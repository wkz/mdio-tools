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

void mdio_prog_push(struct mdio_prog *prog, struct mdio_nl_insn insn)
{
	prog->insns = realloc(prog->insns, (++prog->len) * sizeof(insn));
	memcpy(&prog->insns[prog->len - 1], &insn, sizeof(insn));
}

int mdio_device_dflt_parse_reg(struct mdio_device *dev,
			       int *argcp, char ***argvp,
			       uint32_t *regs, uint32_t *rege)
{
	unsigned long long rs, re, base = 0;
	char *arg, *str, *end;

	errno = 0;

	arg = argv_pop(argcp, argvp);
	if (!arg) {
		fprintf(stderr, "ERROR: Expected register\n");
		return EINVAL;
	}

	str = arg;
	rs = strtoull(str, &end, 0);
	if (errno)
		goto inval;

	switch (*end) {
	case '\0':
		re = rs;
		break;
	case '+':
		base = rs;
		/* fallthrough */
	case '-':
		if (!rege) {
			fprintf(stderr, "ERROR: Unexpected register range\n");
			return EINVAL;
		}

		str = end + 1;
		re = strtoull(str, &end, 0);
		if (errno) {
			fprintf(stderr, "ERROR: \"%s\" is not a valid register range\n", arg);
			return EINVAL;
		}

		re += base;
		break;
	default:
	inval:
		fprintf(stderr, "ERROR: \"%s\" is not a valid register\n", arg);
		return EINVAL;
	}

	if (rs > dev->mem.max) {
		fprintf(stderr, "ERROR: Register %llu is out of range "
			"[0-%"PRIu32"]\n", rs, dev->mem.max);
		return ERANGE;
	}

	if (re > dev->mem.max) {
		fprintf(stderr, "ERROR: Register %llu is out of range "
			"[0-%"PRIu32"]\n", re, dev->mem.max);
		return ERANGE;
	}

	*regs = rs;

	if (rege)
		*rege = re;

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

int mdio_common_raw_read_cb(uint32_t *data, int len, int err, void *_null)
{
	if (len != 1)
		return 1;

	printf("0x%4.4x\n", *data);
	return err;
}

int mdio_common_raw_write_cb(uint32_t *data, int len, int err, void *_null)
{
	if (len != 0)
		return 1;

	return err;
}

int mdio_common_raw_exec(struct mdio_device *dev, int argc, char **argv)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	mdio_xfer_cb_t cb = mdio_common_raw_read_cb;
	uint32_t reg, val, mask;
	int err;

	err = mdio_device_parse_reg(dev, &argc, &argv, &reg, NULL);
	if (err)
		return err;

	if (argv_peek(argc, argv)) {
		cb = mdio_common_raw_write_cb;

		err = mdio_device_parse_val(dev, &argc, &argv, &val, &mask);
		if (err)
			return err;
	}

	if (argv_peek(argc, argv)) {
		fprintf(stderr, "ERROR: Unexpected argument");
		return EINVAL;
	}

	if ((cb == mdio_common_raw_write_cb) && mask) {
		err = dev->driver->read(dev, &prog, reg);
		if (err)
			return err;
		mdio_prog_push(&prog, INSN(AND, REG(0), IMM(mask), REG(0)));
		mdio_prog_push(&prog, INSN(OR,  REG(0), IMM(val),  REG(0)));
		err = dev->driver->write(dev, &prog, reg, REG(0));
		if (err)
			return err;
	} else if (cb == mdio_common_raw_write_cb) {
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

	if (err)
		printf("Benchmark failed after ");
	else
		printf("Performed 1000 reads in ");

	if (end.tv_sec)
		printf("%"PRId64".%2.2"PRId64"s\n", (int64_t)end.tv_sec, (int64_t)end.tv_nsec / 10000000);
	else if (end.tv_nsec > 1000000)
		printf("%"PRId64"ms\n", (int64_t)end.tv_nsec / 1000000);
	else if (end.tv_nsec > 1000)
		printf("%"PRId64"us\n", (int64_t)end.tv_nsec / 1000);
	else
		printf("%"PRId64"ns\n", (int64_t)end.tv_nsec);

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

		mdio_prog_push(&prog, INSN(ADD, IMM(val), IMM(0), REG(7)));
		err = dev->driver->write(dev, &prog, reg, REG(7));
		if (err)
			return err;
	} else {
		err = dev->driver->read(dev, &prog, reg);
		if (err)
			return err;

		mdio_prog_push(&prog, INSN(ADD, REG(0), IMM(0), REG(7)));
	}

	if (argv_peek(argc, argv)) {
		fprintf(stderr, "ERROR: Unexpected argument");
		return EINVAL;
	}


	mdio_prog_push(&prog, INSN(ADD, IMM(0), IMM(0), REG(6)));

	loop = prog.len;

	err = dev->driver->read(dev, &prog, reg);
	if (err)
		return err;

	mdio_prog_push(&prog, INSN(JEQ, REG(0), REG(7), IMM(1)));
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mdio_prog_push(&prog, INSN(ADD, REG(6), IMM(1), REG(6)));
	mdio_prog_push(&prog, INSN(JNE, REG(6), IMM(1000), GOTO(prog.len, loop)));

	clock_gettime(CLOCK_MONOTONIC, &start);
	err = mdio_xfer_timeout(dev->bus, &prog, mdio_common_bench_cb, &start, 10000);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: Bench operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}

struct reg_range {
	uint32_t start;
	uint32_t end;
};

int mdio_common_dump_cb(uint32_t *data, int len, int err, void *_range)
{
	struct reg_range *range = _range;
	uint32_t reg;

	if (len != (int)(range->end - range->start + 1))
		return 1;

	for (reg = range->start; reg <= range->end; reg++)
		printf("0x%04x: 0x%04x\n", reg, *data++);

	return err;
}

int mdio_common_dump_exec_one(struct mdio_device *dev, int *argc, char ***argv)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	struct reg_range range;
	uint32_t reg;
	int err;

	err = mdio_device_parse_reg(dev, argc, argv, &range.start, &range.end);
	if (err)
		return err;

	/* Can't emit a loop, since there's no way to pass the (mdio)
	 * register in a (mdio-netlink) register - so we unroll it. */
	for (reg = range.start; reg <= range.end; reg++) {
		err = dev->driver->read(dev, &prog, reg);
		if (err)
			return err;

		mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	}

	err = mdio_xfer_timeout(dev->bus, &prog, mdio_common_dump_cb, &range, 10000);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: Dump operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}

int mdio_common_dump_exec(struct mdio_device *dev, int argc, char **argv)
{
	int err;

	while (argv_peek(argc, argv)) {
		err = mdio_common_dump_exec_one(dev, &argc, &argv);
		if (err)
			return err;
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
	} else if (!strcmp(argv[0], "dump")) {
		argv_pop(&argc, &argv);
		return mdio_common_dump_exec(dev, argc, argv);
	}

	return mdio_common_raw_exec(dev, argc, argv);
}



struct mdio_xfer_data {
	mdio_xfer_cb_t cb;
	void *arg;
	int err;
};

static int mdio_xfer_cb(const struct nlmsghdr *nlh, void *_xfer)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[MDIO_NLA_MAX + 1] = {};
	struct mdio_xfer_data *xfer = _xfer;
	uint32_t *data;
	int len, err;

	mnl_attr_parse(nlh, sizeof(*genl), parse_attrs, tb);

	if (tb[MDIO_NLA_ERROR])
		xfer->err = (int)mnl_attr_get_u32(tb[MDIO_NLA_ERROR]);

	if (!tb[MDIO_NLA_DATA])
		return MNL_CB_ERROR;

	len = mnl_attr_get_payload_len(tb[MDIO_NLA_DATA]) / sizeof(uint32_t);
	data = mnl_attr_get_payload(tb[MDIO_NLA_DATA]);

	err = xfer->cb(data, len, xfer->err, xfer->arg);
	return err ? MNL_CB_ERROR : MNL_CB_OK;
}

int mdio_xfer_timeout(const char *bus, struct mdio_prog *prog,
		      mdio_xfer_cb_t cb, void *arg, uint16_t timeout_ms)
{
	struct mdio_xfer_data xfer = { .cb = cb, .arg = arg };
	struct nlmsghdr *nlh;
	int err;

	nlh = msg_init(MDIO_GENL_XFER, NLM_F_REQUEST | NLM_F_ACK);
	if (!nlh)
		return -ENOMEM;

	mnl_attr_put_strz(nlh, MDIO_NLA_BUS_ID, bus);
	mnl_attr_put(nlh, MDIO_NLA_PROG, prog->len * sizeof(*prog->insns),
		     prog->insns);

	mnl_attr_put_u16(nlh, MDIO_NLA_TIMEOUT, timeout_ms);

	err = msg_query(nlh, mdio_xfer_cb, &xfer);
	return xfer.err ? : err;
}

int mdio_xfer(const char *bus, struct mdio_prog *prog,
	      mdio_xfer_cb_t cb, void *arg)
{
	return mdio_xfer_timeout(bus, prog, cb, arg, 1000);
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
