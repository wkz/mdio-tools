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
				.max = 31,
			},
		},
	};
	char *arg;

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &pdev.id, false))
		return 1;

	arg = argv_peek(argc, argv);
	if (!arg || !strcmp(arg, "status"))
		return phy_exec_status(&pdev, argc, argv);

	return mdio_common_exec(&pdev.dev, argc, argv);
}
DEFINE_CMD("phy", phy_exec);

int mmd_exec_with(const struct mdio_driver *drv,
		  const char *bus, int argc, char **argv)
{
	struct phy_device pdev = {
		.dev = {
			.bus = bus,
			.driver = drv,

			.mem = {
				.stride = 1,
				.width = 16,
				.max = UINT16_MAX,
			},
		},
	};
	char *arg;

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &pdev.id, true))
		return 1;

	if (!(pdev.id & MDIO_PHY_ID_C45)) {
		fprintf(stderr, "ERROR: Expected Clause 45 (PRTAD:DEVAD) address\n");
		return 1;
	}

	return mdio_common_exec(&pdev.dev, argc, argv);
}

int mmd_exec(const char *bus, int argc, char **argv)
{
	return mmd_exec_with(&phy_driver, bus, argc, argv);
}
DEFINE_CMD("mmd", mmd_exec);

static int mmd_c22_read(struct mdio_device *dev, struct mdio_prog *prog,
			uint32_t reg)
{
	struct phy_device *pdev = (void *)dev;
	uint8_t prtad = (pdev->id & MDIO_PHY_ID_PRTAD) >> 5;
	uint8_t devad = pdev->id & MDIO_PHY_ID_DEVAD;
	uint16_t ctrl = devad;

	/* Set the address */
	ctrl = devad;
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(13),  IMM(ctrl)));
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(14),  IMM(reg)));

	/* Read out the data */
	ctrl |= 1 << 14;
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(13),  IMM(ctrl)));
	mdio_prog_push(prog, INSN(READ,  IMM(prtad), IMM(14),  REG(0)));
	return 0;
}

static int mmd_c22_write(struct mdio_device *dev, struct mdio_prog *prog,
			 uint32_t reg, uint32_t val)
{
	struct phy_device *pdev = (void *)dev;
	uint8_t prtad = (pdev->id & MDIO_PHY_ID_PRTAD) >> 5;
	uint8_t devad = pdev->id & MDIO_PHY_ID_DEVAD;
	uint16_t ctrl = devad;

	/* Set the address */
	ctrl = devad;
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(13),  IMM(ctrl)));
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(14),  IMM(reg)));

	/* Write the data */
	ctrl |= 1 << 14;
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(13),  IMM(ctrl)));
	mdio_prog_push(prog, INSN(WRITE, IMM(prtad), IMM(14),  val));
	return 0;
}

static const struct mdio_driver mmd_c22_driver = {
	.read = mmd_c22_read,
	.write = mmd_c22_write,
};

int mmd_c22_exec(const char *bus, int argc, char **argv)
{
	return mmd_exec_with(&mmd_c22_driver, bus, argc, argv);
}
DEFINE_CMD("mmd-c22", mmd_c22_exec);
