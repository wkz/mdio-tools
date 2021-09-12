#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

struct phy_device {
	struct mdio_device dev;
	uint16_t id;
};

int phy_read(struct mdio_device *dev, struct mdio_prog *prog, uint32_t reg)
{
	struct phy_device *pdev = (void *)dev;

	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(reg),  REG(0)));
	return 0;
}

int phy_write(struct mdio_device *dev, struct mdio_prog *prog,
	      uint32_t reg, uint32_t val)
{
	struct phy_device *pdev = (void *)dev;

	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(reg),  val));
	return 0;
}

static const struct mdio_driver phy_driver = {
	.read = phy_read,
	.write = phy_write,
};

int phy_status_cb(uint32_t *data, int len, int err, void *_null)
{
	if (len != 4)
		return 1;

	if (data[2] == 0xffff && data[3] == 0xffff) {
		fprintf(stderr, "No device found\n");
		return 1;
	}

	print_phy_bmcr(data[0]);
	putchar('\n');
	print_phy_bmsr(data[1]);
	putchar('\n');
	print_phy_id(data[2], data[3]);

	return err;
}

int phy_exec_status(struct phy_device *pdev, int argc, char **argv)
{
	struct mdio_nl_insn insns[] = {
		INSN(READ,  IMM(pdev->id), IMM(0),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
		INSN(READ,  IMM(pdev->id), IMM(1),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
		INSN(READ,  IMM(pdev->id), IMM(2),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
		INSN(READ,  IMM(pdev->id), IMM(3),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
	};
	struct mdio_prog prog = MDIO_PROG_FIXED(insns);
	int err;

	err = mdio_xfer(pdev->dev.bus, &prog, phy_status_cb, NULL);
	if (err) {
		fprintf(stderr, "ERROR: Unable to read status (%d)\n", err);
		return 1;
	}

	return 0;
}

int phy_exec(const char *bus, int argc, char **argv)
{
	struct phy_device pdev = {
		.dev = {
			.bus = bus,
			.driver = &phy_driver,

			.mem = {
				.stride = 1,
				.width = 16,
			},
		},
	};
	char *arg;

	argv_pop(&argc, &argv);

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &pdev.id, true))
		return 1;

	pdev.dev.mem.max = (pdev.id & MDIO_PHY_ID_C45) ? UINT16_MAX : 31;

	arg = argv_peek(argc, argv);
	if (!arg || !strcmp(arg, "status"))
		return phy_exec_status(&pdev, argc, argv);

	return mdio_common_exec(&pdev.dev, argc, argv);
}
DEFINE_CMD(phy, phy_exec);
