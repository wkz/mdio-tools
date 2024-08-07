#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

#define MVA_PAGE 0x16
#define MVA_PAGE_COPPER 0
#define MVA_PAGE_FIBER  1

struct mva_device {
	struct mdio_device dev;
	uint16_t id;
};

int mva_read(struct mdio_device *dev, struct mdio_prog *prog, uint32_t reg)
{
	struct mva_device *pdev = (void *)dev;
	uint8_t page;

	page = reg >> 16;
	reg &= 0x1f;

	/* Save current page in R1 and write the requested one, if they differ. */
	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(MVA_PAGE),  REG(1)));
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(MVA_PAGE),  IMM(page)));

	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(reg),  REG(0)));

	/* Restore old page if we changed it. */
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(MVA_PAGE),  REG(1)));
	return 0;
}

int mva_write(struct mdio_device *dev, struct mdio_prog *prog,
	      uint32_t reg, uint32_t val)
{
	struct mva_device *pdev = (void *)dev;
	uint8_t page;

	page = reg >> 16;
	reg &= 0x1f;

	/* Save current page in R1 and write the requested one, if they differ. */
	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(MVA_PAGE),  REG(1)));
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(MVA_PAGE),  IMM(page)));

	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(reg),  val));

	/* Restore old page if we changed it. */
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(MVA_PAGE),  REG(1)));
	return 0;
}

static int mva_parse_reg(struct mdio_device *dev, int *argcp, char ***argvp,
			 uint32_t *regs, uint32_t *rege)
{
	char *str, *tok, *end;
	unsigned long r;
	uint8_t page;

	if (rege) {
		fprintf(stderr, "ERROR: Implement ranges\n");
		return ENOSYS;
	}

	str = argv_pop(argcp, argvp);
	tok = str ? strtok(str, ":") : NULL;
	if (!tok) {
		fprintf(stderr, "ERROR: PAGE:REG\n");
		return EINVAL;
	} else if (!strcmp(tok, "copper") || !strcmp(tok, "cu")) {
		page = MVA_PAGE_COPPER;
	} else if (!strcmp(tok, "fiber") || !strcmp(tok, "fibre")) {
		page = MVA_PAGE_FIBER;
	} else {
		r = strtoul(tok, &end, 0);
		if (*end) {
			fprintf(stderr, "ERROR: \"%s\" is not a valid page\n", tok);
			return EINVAL;
		}

		if (r > 255) {
			fprintf(stderr, "ERROR: page %lu is out of range [0-255]\n", r);
			return EINVAL;
		}

		page = r;
	}

	tok = strtok(NULL, ":");
	if (!tok) {
		fprintf(stderr, "ERROR: Expected REG\n");
		return EINVAL;
	}

	r = strtoul(tok, &end, 0);
	if (*end) {
		fprintf(stderr, "ERROR: \"%s\" is not a valid register\n",
			tok);
		return EINVAL;
	}

	if (r > 31) {
		fprintf(stderr, "ERROR: register %lu is out of range [0-31]\n", r);
		return EINVAL;
	}

	*regs = (page << 16) | r;
	return 0;
}

static const struct mdio_driver mva_driver = {
	.read = mva_read,
	.write = mva_write,

	.parse_reg = mva_parse_reg,
};

int mva_status_cb(uint32_t *data, int len, int err, void *_null)
{
	if (len != 5)
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

	printf("Current page: %u\n", data[4] & 0xff);
	return err;
}

int mva_exec_status(struct mva_device *pdev, int argc, char **argv)
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
		INSN(READ,  IMM(pdev->id), IMM(MVA_PAGE),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
	};
	struct mdio_prog prog = MDIO_PROG_FIXED(insns);
	int err;

	err = mdio_xfer(pdev->dev.bus, &prog, mva_status_cb, NULL);
	if (err) {
		fprintf(stderr, "ERROR: Unable to read status (%d)\n", err);
		return 1;
	}

	return 0;
}

int mva_exec(const char *bus, int argc, char **argv)
{
	struct mva_device pdev = {
		.dev = {
			.bus = bus,
			.driver = &mva_driver,

			.mem = {
				.stride = 1,
				.width = 16,
			},
		},
	};
	char *arg;

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &pdev.id, true))
		return 1;

	arg = argv_peek(argc, argv);
	if (!arg || !strcmp(arg, "status"))
		return mva_exec_status(&pdev, argc, argv);

	return mdio_common_exec(&pdev.dev, argc, argv);
}
DEFINE_CMD("mva", mva_exec);
