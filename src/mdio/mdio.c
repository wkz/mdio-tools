#include <assert.h>
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/mdio.h>
#include <linux/mdio-netlink.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "mdio.h"

static int mdio_parse_bus_cb(const char *bus, void *_id)
{
	char **id = _id;

	*id = strdup(bus);
	return 1;
}

int mdio_parse_bus(const char *str, char **bus)
{
	*bus = NULL;
	mdio_for_each(str, mdio_parse_bus_cb, bus);

	if (*bus)
		return 0;

	fprintf(stderr, "ERROR: \"%s\" does not match any known MDIO bus", str);
	return ENODEV;
}

int mdio_parse_dev(const char *str, uint16_t *dev, bool allow_c45)
{
	unsigned long p, d = 0;
	char *end;

	p = strtoul(str, &end, 0);
	if (!end[0]) {
		allow_c45 = false;
		goto c22;
	}

	if (end[0] != ':')
		/* not clause 45 either */
		goto err_invalid;

	if (!allow_c45) {
		fprintf(stderr, "ERROR: Clause-45 addressing not allowed\n");
		return EINVAL;
	}

	d = strtoul(end + 1, &end, 0);
	if (end[0])
		goto err_invalid;

	if (allow_c45 && (d > 31)) {
		fprintf(stderr, "ERROR: Device %lu is out of range [0-31]\n", d);
		return ERANGE;
	}

c22:
	if (p > 31) {
		fprintf(stderr, "ERROR: %s %lu is out of range [0-31]\n",
			allow_c45 ? "Port" : "Device", d);
		return ERANGE;
	}

	*dev = allow_c45 ? mdio_phy_id_c45(p, d) : p;
	return 0;

err_invalid:
	fprintf(stderr, "ERROR: \"%s\" is not a valid device\n", str);
	return EINVAL;
}

int mdio_parse_reg(const char *str, uint16_t *reg, bool is_c45)
{
	unsigned long r;
	char *end;

	r = strtoul(str, &end, 0);
	if (end[0]) {
		fprintf(stderr, "ERROR: \"%s\" is not a valid register", str);
		return EINVAL;
	}

	if (r > (is_c45 ? UINT16_MAX : 31)) {
		fprintf(stderr, "ERROR: Register %lu is out of range [0-%u]",
			r, is_c45 ? UINT16_MAX : 31);
		return ERANGE;
	}

	*reg = r;
	return 0;
}

int mdio_parse_reg_range(const char *str, uint16_t *regs, uint16_t *rege,
			 bool is_c45)
{
	unsigned long s, e = 0;
	char *delim, *end;

	s = strtoul(str, &delim, 0);
	switch (*delim) {
	case '\0':
		e = is_c45 ? 31 : s + 127;
		break;
	case '+':
		e += s;
		/* fall-through */
	case '-':
		e += strtoul(delim + 1, &end, 0);

		if (!(*end))
			break;

		/* fall-through */
	default:
		fprintf(stderr, "ERROR: \"%s\" is not a valid register range",
			str);
		return EINVAL;
	}


	if (e > (is_c45 ? UINT16_MAX : 31)) {
		fprintf(stderr, "ERROR: Register %lu is out of range [0-%u]",
			e, is_c45 ? UINT16_MAX : 31);
		return ERANGE;
	}

	*regs = s;
	*rege = e;
	return 0;
}

int mdio_parse_val(const char *str, uint16_t *val, uint16_t *mask)
{
	unsigned long v, m = 0;
	char *end;

	v = strtoul(str, &end, 0);
	if (!end[0])
		goto done;

	if (end[0] != '/')
		goto err_invalid;

	if (!mask) {
		fprintf(stderr, "ERROR: Masking of value not allowed");
		return EINVAL;
	}

	m = strtoul(end + 1, &end, 0);
	if (end[0])
		goto err_invalid;

done:
	if (v > UINT16_MAX) {
		fprintf(stderr, "ERROR: Value %#lx is out of range [0-%u]",
			v, UINT16_MAX);
		return ERANGE;
	}

	if (m > UINT16_MAX) {
		fprintf(stderr, "ERROR: Mask %#lx is out of range [0-%u]",
			m, UINT16_MAX);
		return ERANGE;
	}

	*val = v;
	if (mask)
		*mask = m;
	return 0;

err_invalid:
	fprintf(stderr, "ERROR: \"%s\" is not a valid register value", str);
	return EINVAL;
}

static char buf[0x1000] __attribute__ ((aligned (NLMSG_ALIGNTO)));
static const size_t len = 0x1000;
static uint16_t mdio_family;

static int parse_attrs(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	tb[type] = attr;

	return MNL_CB_OK;
}

static struct mnl_socket *msg_send(int bus, struct nlmsghdr *nlh)
{
	struct mnl_socket *nl;
	int ret;

	nl = mnl_socket_open(bus);
	if (nl == NULL) {
		perror("mnl_socket_open");
		return NULL;
	}

	ret = mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID);
	if (ret < 0) {
		perror("mnl_socket_bind");
		return NULL;
	}

	ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
	if (ret < 0) {
		perror("mnl_socket_send");
		return NULL;
	}

	return nl;
}

static int msg_recv(struct mnl_socket *nl, mnl_cb_t callback, void *data, int seq)
{
	unsigned int portid;
	int ret;

	portid = mnl_socket_get_portid(nl);

	ret = mnl_socket_recvfrom(nl, buf, len);
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, callback, data);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, len);
	}

	return ret;
}

static int msg_query(struct nlmsghdr *nlh, mnl_cb_t callback, void *data)
{
	unsigned int seq;
	struct mnl_socket *nl;
	int ret;

	seq = time(NULL);
	nlh->nlmsg_seq = seq;

	nl = msg_send(NETLINK_GENERIC, nlh);
	if (!nl)
		return -ENOTSUP;

	ret = msg_recv(nl, callback, data, seq);
	mnl_socket_close(nl);
	return ret;
}


struct nlmsghdr *msg_init(int cmd, int flags)
{
	struct genlmsghdr *genl;
	struct nlmsghdr *nlh;

	nlh = mnl_nlmsg_put_header(buf);
	if (!nlh)
		return NULL;

	nlh->nlmsg_type	 = mdio_family;
	nlh->nlmsg_flags = flags;

	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
	genl->cmd = cmd;
	genl->version = 1;

	return nlh;
}

void mdio_prog_push(struct mdio_prog *prog, struct mdio_nl_insn insn)
{
	prog->insns = realloc(prog->insns, (++prog->len) * sizeof(insn));
	memcpy(&prog->insns[prog->len - 1], &insn, sizeof(insn));
}

int mdio_raw_read_cb(uint32_t *data, int len, int err, void *_null)
{
	if (len != 1)
		return 1;

	printf("0x%4.4x\n", *data);
	return err;
}

int mdio_raw_write_cb(uint32_t *data, int len, int err, void *_null)
{
	if (len != 0)
		return 1;

	return err;
}

int mdio_raw_exec(struct mdio_ops *ops, int argc, char **argv)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	mdio_xfer_cb_t cb = mdio_raw_read_cb;
	uint16_t dev, reg, val, mask;
	int err;

	switch (argc) {
	case 3:
		cb = mdio_raw_write_cb;
		if (mdio_parse_val(argv[2], &val, &mask))
			return 1;

		/* fall-through */
	case 2:
		if (mdio_parse_dev(argv[0], &dev, true))
			return 1;

		if (mdio_parse_reg(argv[1], &reg, dev & MDIO_PHY_ID_C45))
			return 1;

		break;

	default:
		ops->usage(stderr);
		return 1;
	}

	if ((cb == mdio_raw_write_cb) && mask) {
		err = ops->push_read(ops, &prog, dev, reg);
		if (err)
			return err;
		mdio_prog_push(&prog, INSN(AND, REG(0), IMM(mask), REG(0)));
		mdio_prog_push(&prog, INSN(OR,  REG(0), IMM(val),  REG(0)));
		err = ops->push_write(ops, &prog, dev, reg, REG(0));
		if (err)
			return err;
	} else if (cb == mdio_raw_write_cb) {
		err = ops->push_write(ops, &prog, dev, reg, IMM(val));
		if (err)
			return err;
	} else {
		err = ops->push_read(ops, &prog, dev, reg);
		if (err)
			return err;
		mdio_prog_push(&prog, INSN(EMIT,  REG(0),   0,         0));
	}

	err = mdio_xfer(ops->bus, &prog, cb, NULL);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: Raw operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}

int mdio_device_dflt_parse_reg(struct mdio_device *dev,
			       int *argcp, char ***argvp,
			       uint32_t *regs, uint32_t *rege)
{
	unsigned long long r;
	char *str, *end;

	if (rege) {
		fprintf(stderr, "ERROR: Implement ranges\n");
		return ENOSYS;
	}

	str = argv_pop(argcp, argvp);
	if (!str) {
		fprintf(stderr, "ERROR: Expected register\n");
		return EINVAL;
	}

	r = strtoull(str, &end, 0);
	if (end[0]) {
		fprintf(stderr, "ERROR: \"%s\" is not a valid register\n", str);
		return EINVAL;
	}

	if (r > dev->mem.max) {
		fprintf(stderr, "ERROR: Register %llu is out of range "
			"[0-%"PRIu32"]\n", r, dev->mem.max);
		return ERANGE;
	}

	*regs = r;
	return 0;
}

int mdio_device_parse_reg(struct mdio_device *dev, int *argcp, char ***argvp,
			  uint32_t *regs, uint32_t *rege)
{
	if (dev->driver->parse_reg)
		return dev->driver->parse_reg(dev, argcp, argvp, regs, rege);

	return mdio_device_dflt_parse_reg(dev, argcp, argvp, regs, rege);
}

int mdio_device_dflt_parse_val(struct mdio_device *dev,
			       int *argcp, char ***argvp,
			       uint32_t *val, uint32_t *mask)
{
	unsigned long long max = (1 << dev->mem.width) - 1;
	unsigned long long v, m = 0;
	char *str, *end;

	if (dev->driver->parse_val)
		return dev->driver->parse_val(dev, argcp, argvp, val, mask);

	str = argv_pop(argcp, argvp);
	if (!str) {
		fprintf(stderr, "ERROR: Expected register\n");
		return EINVAL;
	}

	v = strtoull(str, &end, 0);
	if (!end[0])
		goto done;

	if (end[0] != '/')
		goto err_invalid;

	if (!mask) {
		fprintf(stderr, "ERROR: Masking of value not allowed");
		return EINVAL;
	}

	m = strtoull(end + 1, &end, 0);
	if (end[0])
		goto err_invalid;

done:
	if (v > max) {
		fprintf(stderr, "ERROR: Value %#llx is out of range "
			"[0-%#llx]", v, max);
		return ERANGE;
	}

	if (m > max) {
		fprintf(stderr, "ERROR: Mask %#llx is out of range "
			"[0-%#llx]", m, max);
		return ERANGE;
	}

	*val = v;
	if (mask)
		*mask = m;
	return 0;

err_invalid:
	fprintf(stderr, "ERROR: \"%s\" is not a valid register value\n", str);
	return EINVAL;
}

int mdio_device_parse_val(struct mdio_device *dev, int *argcp, char ***argvp,
			  uint32_t *val, uint32_t *mask)
{
	if (dev->driver->parse_val)
		return dev->driver->parse_val(dev, argcp, argvp, val, mask);

	return mdio_device_dflt_parse_val(dev, argcp, argvp, val, mask);
}

int mdio_common_raw_exec(struct mdio_device *dev, int argc, char **argv)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	mdio_xfer_cb_t cb = mdio_raw_read_cb;
	uint32_t reg, val, mask;
	int err;

	err = mdio_device_parse_reg(dev, &argc, &argv, &reg, NULL);
	if (err)
		return err;

	if (argv_peek(argc, argv)) {
		cb = mdio_raw_write_cb;

		err = mdio_device_parse_val(dev, &argc, &argv, &val, &mask);
		if (err)
			return err;
	}

	if (argv_peek(argc, argv)) {
		fprintf(stderr, "ERROR: Unexpected argument");
		return EINVAL;
	}

	if ((cb == mdio_raw_write_cb) && mask) {
		err = dev->driver->read(dev, &prog, reg);
		if (err)
			return err;
		mdio_prog_push(&prog, INSN(AND, REG(0), IMM(mask), REG(0)));
		mdio_prog_push(&prog, INSN(OR,  REG(0), IMM(val),  REG(0)));
		err = dev->driver->write(dev, &prog, reg, REG(0));
		if (err)
			return err;
	} else if (cb == mdio_raw_write_cb) {
		err = dev->driver->write(dev, &prog, reg, IMM(val));
		if (err)
			return err;
	} else {
		err = dev->driver->read(dev, &prog, reg);
		if (err)
			return err;
		mdio_prog_push(&prog, INSN(EMIT,  REG(0),   0,         0));
	}

	err = mdio_xfer(dev->bus, &prog, cb, NULL);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: Raw operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}

int mdio_common_bench_cb(uint32_t *data, int len, int err, void *_start)
{
	struct timespec end, *start = _start;

	clock_gettime(CLOCK_MONOTONIC, &end);

	if (len) {
		int i;

		printf("Read back %d incorrect values:\n", len);
		err = 1;

		for (i = 0; i < len; i++)
			printf("\t0x%4.4x\n", data[i]);
	}

	end.tv_sec -= start->tv_sec;
	end.tv_nsec -= start->tv_nsec;
	if (end.tv_nsec < 0) {
		end.tv_nsec += 1000000000;
		end.tv_sec--;
	}

	printf("Performed 1000 reads in ");

	if (end.tv_sec)
		printf("%ld.%2.2lds\n", end.tv_sec, end.tv_nsec / 10000000);
	else if (end.tv_nsec > 1000000)
		printf("%ldms\n", end.tv_nsec / 1000000);
	else if (end.tv_nsec > 1000)
		printf("%ldus\n", end.tv_nsec / 1000);
	else
		printf("%ldns\n", end.tv_nsec);

	return err;
}

int mdio_common_bench_exec(struct mdio_device *dev, int argc, char **argv)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	struct timespec start;
	uint32_t reg, val = 0;
	int err, loop;

	err = mdio_device_parse_reg(dev, &argc, &argv, &reg, NULL);
	if (err)
		return err;

	if (argv_peek(argc, argv)) {
		err = mdio_device_parse_val(dev, &argc, &argv, &val, NULL);
		if (err)
			return err;

		mdio_prog_push(&prog, INSN(ADD, IMM(val), IMM(0), REG(1)));
		err = dev->driver->write(dev, &prog, reg, REG(1));
		if (err)
			return err;
	} else {
		err = dev->driver->read(dev, &prog, reg);
		if (err)
			return err;

		mdio_prog_push(&prog, INSN(ADD, REG(0), IMM(0), REG(1)));
	}

	if (argv_peek(argc, argv)) {
		fprintf(stderr, "ERROR: Unexpected argument");
		return EINVAL;
	}


	mdio_prog_push(&prog, INSN(ADD, IMM(0), IMM(0), REG(2)));

	loop = prog.len;

	err = dev->driver->read(dev, &prog, reg);
	if (err)
		return err;

	mdio_prog_push(&prog, INSN(JEQ, REG(0), REG(1), IMM(1)));
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mdio_prog_push(&prog, INSN(ADD, REG(2), IMM(1), REG(2)));
	mdio_prog_push(&prog, INSN(JNE, REG(2), IMM(1000), GOTO(prog.len, loop)));

	clock_gettime(CLOCK_MONOTONIC, &start);
	err = mdio_xfer(dev->bus, &prog, mdio_common_bench_cb, &start);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: Bench operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}

int mdio_common_exec(struct mdio_device *dev, int argc, char **argv)
{
	if (!argc)
		return 1;

	if (!strcmp(argv[0], "raw")) {
		argv_pop(&argc, &argv);
		return mdio_common_raw_exec(dev, argc, argv);
	} else if (!strcmp(argv[0], "bench")) {
		argv_pop(&argc, &argv);
		return mdio_common_bench_exec(dev, argc, argv);
	}

	return mdio_common_raw_exec(dev, argc, argv);
}



struct mdio_xfer_data {
	mdio_xfer_cb_t cb;
	void *arg;
};

static int mdio_xfer_cb(const struct nlmsghdr *nlh, void *_xfer)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct mdio_xfer_data *xfer = _xfer;
	struct nlattr *tb[MDIO_NLA_MAX + 1] = {};
	int len, err, xerr = 0;
	uint32_t *data;

	mnl_attr_parse(nlh, sizeof(*genl), parse_attrs, tb);

	if (tb[MDIO_NLA_ERROR])
		xerr = (int)mnl_attr_get_u32(tb[MDIO_NLA_ERROR]);

	if (!tb[MDIO_NLA_DATA])
		return MNL_CB_ERROR;

	len = mnl_attr_get_payload_len(tb[MDIO_NLA_DATA]) / sizeof(uint32_t);
	data = mnl_attr_get_payload(tb[MDIO_NLA_DATA]);

	err = xfer->cb(data, len, xerr, xfer->arg);
	return err ? MNL_CB_ERROR : MNL_CB_OK;
}

int mdio_xfer(const char *bus, struct mdio_prog *prog,
	      mdio_xfer_cb_t cb, void *arg)
{
	struct mdio_xfer_data xfer = { .cb = cb, .arg = arg };
	struct nlmsghdr *nlh;

	nlh = msg_init(MDIO_GENL_XFER, NLM_F_REQUEST | NLM_F_ACK);
	if (!nlh)
		return -ENOMEM;

	mnl_attr_put_strz(nlh, MDIO_NLA_BUS_ID, bus);
	mnl_attr_put(nlh, MDIO_NLA_PROG, prog->len * sizeof(*prog->insns),
		     prog->insns);

	mnl_attr_put_u16(nlh, MDIO_NLA_TIMEOUT, 1000);

	return msg_query(nlh, mdio_xfer_cb, &xfer);
}


int mdio_for_each(const char *match,
		  int (*cb)(const char *bus, void *arg), void *arg)
{
	char gmatch[0x80];
	glob_t gl;
	size_t i;
	int err;

	snprintf(gmatch, sizeof(gmatch), "/sys/class/mdio_bus/%s", match);
	glob(gmatch, 0, NULL, &gl);

	for (err = 0, i = 0; i < gl.gl_pathc; i++) {
		err = cb(&gl.gl_pathv[i][strlen("/sys/class/mdio_bus/")], arg);
		if (err)
			break;
	}

	globfree(&gl);
	return err;
}


static int family_id_cb(const struct nlmsghdr *nlh, void *_null)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[CTRL_ATTR_MAX + 1] = {};

	mnl_attr_parse(nlh, sizeof(*genl), parse_attrs, tb);
	if (!tb[CTRL_ATTR_FAMILY_ID])
		return MNL_CB_ERROR;

	mdio_family = mnl_attr_get_u16(tb[CTRL_ATTR_FAMILY_ID]);
	return MNL_CB_OK;
}

int mdio_modprobe(void)
{
	int wstatus;
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		return -errno;
	} else if (!pid) {
		execl("/sbin/modprobe", "modprobe", "mdio-netlink", NULL);
		exit(1);
	}

	if (waitpid(pid, &wstatus, 0) <= 0)
		return -ECHILD;

	if (WIFEXITED(wstatus) && !WEXITSTATUS(wstatus))
		return 0;

	return -EPERM;
}

int mdio_init(void)
{
	struct genlmsghdr *genl;
	struct nlmsghdr *nlh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= GENL_ID_CTRL;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
	genl->cmd = CTRL_CMD_GETFAMILY;
	genl->version = 1;

	mnl_attr_put_u16(nlh, CTRL_ATTR_FAMILY_ID, GENL_ID_CTRL);
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, "mdio");

	return msg_query(nlh, family_id_cb, NULL);
}
