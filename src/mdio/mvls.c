#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

#define BIT(_n) (1 << (_n))

#define MVLS_CMD  0
#define MVLS_CMD_BUSY BIT(15)
#define MVLS_CMD_C22  BIT(12)

#define MVLS_DATA 1

#define MVLS_G1 0x1b
#define MVLS_G2 0x1c

struct mvls_device {
	struct mdio_device dev;
	uint16_t id;

};
#define to_mvls_mdio_ops(_ops) container_of(_ops, struct mvls_mdio_ops, ops)

static inline uint16_t mvls_multi_cmd(uint8_t port, uint8_t reg, bool write)
{
	return MVLS_CMD_BUSY | MVLS_CMD_C22 | ((write ? 1 : 2) << 10)|
		(port << 5) | reg;
}

static void mvls_wait_cmd(struct mdio_prog *prog, uint8_t id)
{
	int retry;

	retry = prog->len;
	mdio_prog_push(prog, INSN(READ, IMM(id), IMM(MVLS_CMD), REG(0)));
	mdio_prog_push(prog, INSN(AND, REG(0), IMM(MVLS_CMD_BUSY), REG(0)));
	mdio_prog_push(prog, INSN(JEQ, REG(0), IMM(MVLS_CMD_BUSY),
				  GOTO(prog->len, retry)));
}

static void mvls_read_to(struct mdio_device *dev, struct mdio_prog *prog,
			 uint32_t reg, uint8_t to)
{
	struct mvls_device *mdev = (void *)dev;
	uint16_t port;

	port = reg >> 16;
	reg &= 0xffff;

	if (!mdev->id) {
		/* Single-chip addressing, the switch uses the entire
		 * underlying bus */
		mdio_prog_push(prog, INSN(READ, IMM(port), IMM(reg), REG(to)));
		return;
	}

	mdio_prog_push(prog, INSN(WRITE, IMM(mdev->id), IMM(MVLS_CMD),
				  IMM(mvls_multi_cmd(port, reg, false))));
	mvls_wait_cmd(prog, mdev->id);
	mdio_prog_push(prog, INSN(READ, IMM(mdev->id), IMM(MVLS_DATA), REG(to)));
}

static int mvls_read(struct mdio_device *dev, struct mdio_prog *prog,
		     uint32_t reg)
{
	mvls_read_to(dev, prog, reg, 0);
	return 0;
}

static int mvls_write(struct mdio_device *dev, struct mdio_prog *prog,
		      uint32_t reg, uint32_t val)
{
	struct mvls_device *mdev = (void *)dev;
	uint16_t port;

	port = reg >> 16;
	reg &= 0xffff;

	if (!mdev->id) {
		/* Single-chip addressing, the switch uses the entire
		 * underlying bus */
		mdio_prog_push(prog, INSN(WRITE, IMM(port), IMM(reg), val));
		return 0;
	}

	mdio_prog_push(prog, INSN(WRITE, IMM(mdev->id), IMM(MVLS_DATA), val));
	mdio_prog_push(prog, INSN(WRITE, IMM(mdev->id), IMM(MVLS_CMD),
				  IMM(mvls_multi_cmd(port, reg, true))));
	mvls_wait_cmd(prog, mdev->id);
	return 0;
}

static int mvls_parse_reg(struct mdio_device *dev, int *argcp, char ***argvp,
			  uint32_t *regs, uint32_t *rege)
{
	unsigned long r;
	char *str, *end;
	uint16_t port;

	if (rege) {
		fprintf(stderr, "ERROR: Implement ranges\n");
		return ENOSYS;
	}

	str = argv_pop(argcp, argvp);
	if (!str) {
		fprintf(stderr, "ERROR: Expected port");
		return EINVAL;
	} else if (!strcmp(str, "global1") || !strcmp(str, "g1")) {
		port = MVLS_G1;
	} else if (!strcmp(str, "global2") || !strcmp(str, "g2")) {
		port = MVLS_G1;
	} else {
		r = strtoul(str, &end, 0);
		if (*end) {
			fprintf(stderr, "ERROR: \"%s\" is not a valid port\n", str);
			return EINVAL;
		}

		if (r > 31) {
			fprintf(stderr, "ERROR: port %lu is out of range [0-31]\n", r);
			return EINVAL;
		}

		port = r;
	}

	str = argv_pop(argcp, argvp);
	if (!str) {
		fprintf(stderr, "ERROR: Expected register");
		return EINVAL;
	}

	r = strtoul(str, &end, 0);
	if (*end) {
		fprintf(stderr, "ERROR: \"%s\" is not a valid register\n",
			str);
		return EINVAL;
	}

	if (r > 31) {
		fprintf(stderr, "ERROR: register %lu is out of range [0-31]\n", r);
		return EINVAL;
	}

	*regs = (port << 16) | r;
	return 0;
}

struct mdio_driver mvls_driver = {
	.read = mvls_read,
	.write = mvls_write,

	.parse_reg = mvls_parse_reg,
};

static int mvls_exec(const char *bus, int argc, char **argv)
{
	struct mvls_device mdev = {
		.dev = {
			.bus = bus,
			.driver = &mvls_driver,

			.mem = {
				.stride = 1,
				.width = 16,
			},
		},
	};
	char *arg;

	argv_pop(&argc, &argv);

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &mdev.id, true))
		return 1;

	arg = argv_peek(argc, argv);
	if (!arg)
		return 1;

	return mdio_common_exec(&mdev.dev, argc, argv);
}
DEFINE_CMD(mvls, mvls_exec);
