#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

struct xrs_device {
	struct mdio_device dev;
	uint16_t id;
};

#define XRS_IBA0 0x10
#define XRS_IBA1 0x11
#define XRS_IBD  0x14

int xrs_read(struct mdio_device *dev, struct mdio_prog *prog, uint32_t reg)
{
	struct xrs_device *xdev = (void *)dev;
	uint16_t iba[2] = { reg & 0xfffe, reg >> 16 };

	mdio_prog_push(prog, INSN(WRITE, IMM(xdev->id), IMM(XRS_IBA1), IMM(iba[1])));
	mdio_prog_push(prog, INSN(WRITE, IMM(xdev->id), IMM(XRS_IBA0), IMM(iba[0])));
	mdio_prog_push(prog, INSN(READ,  IMM(xdev->id), IMM(XRS_IBD),  REG(0)));
	return 0;
}

int xrs_write(struct mdio_device *dev, struct mdio_prog *prog,
	      uint32_t reg, uint32_t val)
{
	struct xrs_device *xdev = (void *)dev;
	uint16_t iba[2] = { (reg & 0xfffe) | 1, reg >> 16 };

	mdio_prog_push(prog, INSN(WRITE, IMM(xdev->id), IMM(XRS_IBD),  IMM(val)));
	mdio_prog_push(prog, INSN(WRITE, IMM(xdev->id), IMM(XRS_IBA1), IMM(iba[1])));
	mdio_prog_push(prog, INSN(WRITE, IMM(xdev->id), IMM(XRS_IBA0), IMM(iba[0])));
	return 0;
}

static const struct mdio_driver xrs_driver = {
	.read = xrs_read,
	.write = xrs_write,
};

int xrs_exec(const char *bus, int argc, char **argv)
{
	struct xrs_device xdev = {
		.dev = {
			.bus = bus,
			.driver = &xrs_driver,

			.mem = {
				.max = UINT32_MAX,
				.stride = 2,
				.width = 16,
			},
		},
	};
	char *arg;

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &xdev.id, true))
		return 1;

	arg = argv_peek(argc, argv);
	if (!arg)
		return 1;

	return mdio_common_exec(&xdev.dev, argc, argv);
}
DEFINE_CMD("xrs", xrs_exec);
