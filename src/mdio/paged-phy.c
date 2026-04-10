#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

struct pphy_page_name {
	const char *name;
	uint8_t page;
};

struct pphy_device {
	struct mdio_device dev;
	uint16_t id;
	uint8_t page_reg;

	const struct pphy_page_name *page_names;
};

int pphy_read(struct mdio_device *dev, struct mdio_prog *prog, uint32_t reg)
{
	struct pphy_device *pdev = (void *)dev;
	uint8_t page;

	page = reg >> 16;
	reg &= 0x1f;

	/* Save current page in R1 and write the requested one, if they differ. */
	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(pdev->page_reg),  REG(1)));
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(pdev->page_reg),  IMM(page)));

	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(reg),  REG(0)));

	/* Restore old page if we changed it. */
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(pdev->page_reg),  REG(1)));
	return 0;
}

int pphy_write(struct mdio_device *dev, struct mdio_prog *prog,
	       uint32_t reg, uint32_t val)
{
	struct pphy_device *pdev = (void *)dev;
	uint8_t page;

	page = reg >> 16;
	reg &= 0x1f;

	/* Save current page in R1 and write the requested one, if they differ. */
	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(pdev->page_reg),  REG(1)));
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(pdev->page_reg),  IMM(page)));

	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(reg),  val));

	/* Restore old page if we changed it. */
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(pdev->page_reg),  REG(1)));
	return 0;
}

static int pphy_parse_page(struct pphy_device *pdev, const char *tok, uint8_t *page)
{
	const struct pphy_page_name *pn;
	unsigned long r;
	char *end;

	for (pn = pdev->page_names; pn && pn->name; pn++) {
		if (!strcmp(tok, pn->name)) {
			*page = pn->page;
			return 0;
		}
	}

	r = strtoul(tok, &end, 0);
	if (*end) {
		fprintf(stderr, "ERROR: \"%s\" is not a valid page\n", tok);
		return EINVAL;
	}

	if (r > 255) {
		fprintf(stderr, "ERROR: page %lu is out of range [0-255]\n", r);
		return EINVAL;
	}

	*page = r;
	return 0;
}

static int pphy_parse_reg(struct mdio_device *dev, int *argcp, char ***argvp,
			  uint32_t *regs, uint32_t *rege)
{
	struct pphy_device *pdev = (void *)dev;
	char *str, *tok;
	uint8_t page;
	int err;

	str = argv_pop(argcp, argvp);
	tok = str ? strtok(str, ":") : NULL;
	if (!tok) {
		fprintf(stderr, "ERROR: PAGE:REG\n");
		return EINVAL;
	}

	err = pphy_parse_page(pdev, tok, &page);
	if (err)
		return err;

	tok = strtok(NULL, ":");
	if (!tok) {
		fprintf(stderr, "ERROR: Expected REG\n");
		return EINVAL;
	}

	if (mdio_parse_range(dev, tok, regs, rege))
		return EINVAL;

	*regs |= (page << 16);

	return 0;
}

int pphy_dump(struct mdio_device *dev, struct mdio_prog *prog,
	      struct reg_range *range)
{
	struct pphy_device *pdev = (void *)dev;
	uint8_t page;
	uint32_t reg;

	page = range->start >> 16;
	range->start &= 0x1f;

	/* Save current page in R1 and write the requested one, if they differ. */
	mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(pdev->page_reg),  REG(1)));
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(pdev->page_reg),  IMM(page)));

	for (reg = range->start; reg <= range->end; reg++) {
		mdio_prog_push(prog, INSN(READ,  IMM(pdev->id), IMM(reg),  REG(0)));
		mdio_prog_push(prog, INSN(EMIT, REG(0), 0, 0));
	}

	/* Restore old page if we changed it. */
	mdio_prog_push(prog, INSN(JEQ,  REG(1), IMM(page),  IMM(1)));
	mdio_prog_push(prog, INSN(WRITE,  IMM(pdev->id), IMM(pdev->page_reg),  REG(1)));

	return 0;
}

static const struct mdio_driver pphy_driver = {
	.read = pphy_read,
	.write = pphy_write,

	.dump = pphy_dump,
	.parse_reg = pphy_parse_reg,
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

int mva_exec_status(struct pphy_device *pdev, int argc, char **argv)
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
		INSN(READ,  IMM(pdev->id), IMM(pdev->page_reg),  REG(0)),
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
	static const struct pphy_page_name page_names[] = {
		{ .name = "copper", .page = 0 },
		{ .name = "cu",     .page = 0 },
		{ .name = "fiber",  .page = 1 },
		{ .name = "fibre",  .page = 1 },

		{ .name = NULL }
	};

	struct pphy_device pdev = {
		.dev = {
			.bus = bus,
			.driver = &pphy_driver,

			.mem = {
				.stride = 1,
				.width = 16,
				.max = 31,
			},
		},
		.page_reg = 0x16,
		.page_names = page_names,
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

int mscc_exec(const char *bus, int argc, char **argv)
{
	struct pphy_device pdev = {
		.dev = {
			.bus = bus,
			.driver = &pphy_driver,

			.mem = {
				.stride = 1,
				.width = 16,
				.max = 31,
			},
		},
		.page_reg = 0x1f,
	};
	char *arg;

	arg = argv_pop(&argc, &argv);
	if (!arg || mdio_parse_dev(arg, &pdev.id, true))
		return 1;

	return mdio_common_exec(&pdev.dev, argc, argv);
}
DEFINE_CMD("mscc", mscc_exec);
