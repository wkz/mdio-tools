#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

void usage(FILE *fp)
{
	fputs("Usage: mdio COMMAND OPTIONS\n"
	      "\n"
	      "Device agnostic commands:\n"
	      "\n"
	      "  help\n"
	      "    Show this usage message.\n"
	      "\n"
 	      "  list [BUS]\n"
	      "    List buses matching BUS, or all if not specified.\n"
	      "\n"
 	      "  dump BUS [PORT:]DEV [REG[-REG|+NUM]]\n"
	      "    Dump multiple registers. For Clause 22 devices, all registers are\n"
	      "    dumped by default. For Clause 45 the default range is [0-127].\n"
	      "\n"
 	      "  raw BUS [PORT:]DEV REG [VAL[/MASK]]\n"
	      "    Raw register access. Without VAL, REG is read. An unmasked VAL will\n"
	      "    do a single write to REG. A masked VAL will perform a read/mask/write\n"
	      "    sequence.\n"
	      "\n"
	      "Device specific commands:\n"
	      "\n"
	      "  mv6\n"
	      "    Commands related to Marvell's MV88E6xxx series of Ethernet switches.\n"
	      "\n"
	      "Common options:\n"
	      "  BUS           The ID of an MDIO bus. Use 'mdio list' to see available\n"
	      "                buses. Uses glob(3) matching to locate bus, i.e. \"fixed*\"\n"
	      "                would typically match against \"fixed-0\".\n"
	      "\n"
	      "  [PORT:]DEV    MDIO device address, either a single 5-bit integer for a\n"
	      "                Clause 22 address, or PORT:DEV for a Clause 45 ditto.\n"
	      "\n"
	      "  REG           Register address, a single 5-bit integer for a Clause 22\n"
	      "                access, or 16 bits for Clause 45.\n"
	      "\n"
	      "  VAL[/MASK]    Register value, 16 bits. Optionally masked using VAL/MASK\n"
	      "                which will read/mask/write the referenced register.\n",
	      fp);
}

int help_exec(int argc, char **argv)
{
	usage(stdout);
	return 0;
}
DEFINE_CMD(help, help_exec);

int bare_push_read(struct mdio_ops *ops, struct mdio_prog *prog,
		   uint16_t dev, uint16_t reg)
{
	mdio_prog_push(prog, INSN(READ,  IMM(dev), IMM(reg),  REG(0)));
	return 0;
}

int bare_push_write(struct mdio_ops *ops, struct mdio_prog *prog,
		    uint16_t dev, uint16_t reg, uint32_t val)
{
	mdio_prog_push(prog, INSN(READ,  IMM(dev), IMM(reg),  val));
	return 0;
}

int dump_exec(int argc, char **argv)
{
	struct mdio_ops ops = {
		.usage = usage,
		.push_read = bare_push_read,
		.push_write = bare_push_write,
	};

	if (argc < 2) {
		usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &ops.bus))
		return 1;

	return mdio_dump_exec(&ops, argc - 2, &argv[2]);
}
DEFINE_CMD(dump, dump_exec);


int raw_exec(int argc, char **argv)
{
	struct mdio_ops ops = {
		.usage = usage,
		.push_read = bare_push_read,
		.push_write = bare_push_write,
	};

	if (argc < 2) {
		usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &ops.bus))
		return 1;

	return mdio_raw_exec(&ops, argc - 2, &argv[2]);
}
DEFINE_CMD(raw, raw_exec);


int status_bus_cb(uint32_t *data, int len, int err, void *_null)
{
	uint16_t dev;

	if (len != MDIO_DEV_MAX * 3)
		return 1;

	printf("\e[7m%4s  %10s  %4s\e[0m\n", "DEV", "PHY-ID", "LINK");
	for (dev = 0; dev < MDIO_DEV_MAX; dev++, data += 3) {
		if (data[1] == 0xffff && data[2] == 0xffff)
			continue;

		printf("0x%2.2x  0x%8.8x  %s\n", dev,
		       (data[1] << 16) | data[2],
		       (data[0] & BMSR_LSTATUS) ? "up" : "down");
	}

	return err;
}

int status_bus(const char *bus)
{
	struct mdio_nl_insn insns[] = {
		INSN(ADD,  IMM(0), IMM(0),  REG(1)),

		INSN(READ, REG(1), IMM(1),  REG(0)),
		INSN(EMIT, REG(0),   0,         0),
		INSN(READ, REG(1), IMM(2),  REG(0)),
		INSN(EMIT, REG(0),   0,         0),
		INSN(READ, REG(1), IMM(3),  REG(0)),
		INSN(EMIT, REG(0),   0,         0),

		INSN(ADD, REG(1), IMM(1), REG(1)),
		INSN(JNE, REG(1), IMM(MDIO_DEV_MAX), IMM(-8)),
	};
	struct mdio_prog prog = MDIO_PROG_FIXED(insns);
	int err;

	err = mdio_xfer(bus, &prog, status_bus_cb, NULL);
	if (err) {
		fprintf(stderr, "ERROR: Unable to read status (%d)\n", err);
		return 1;
	}

	return 0;
}

int status_phy_cb(uint32_t *data, int len, int err, void *_null)
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

int status_phy(const char *bus, uint16_t dev)
{
	struct mdio_nl_insn insns[] = {
		INSN(READ,  IMM(dev), IMM(0),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
		INSN(READ,  IMM(dev), IMM(1),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
		INSN(READ,  IMM(dev), IMM(2),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
		INSN(READ,  IMM(dev), IMM(3),  REG(0)),
		INSN(EMIT,  REG(0),   0,         0),
	};
	struct mdio_prog prog = MDIO_PROG_FIXED(insns);
	int err;

	err = mdio_xfer(bus, &prog, status_phy_cb, NULL);
	if (err) {
		fprintf(stderr, "ERROR: Unable to read status (%d)\n", err);
		return 1;
	}

	return 0;
}

int status_exec(int argc, char **argv)
{
	uint16_t dev;
	char *bus;
	int err;

	if (argc < 2 || argc > 3) {
		usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &bus))
		return 1;

	if (argc == 2) {
		err = status_bus(bus);
	} else if (argc == 3) {
		/* TODO: Figure out how BMCR/BMSR or equivalent looks
		 * for C45 PHYs. */
		err = mdio_parse_dev(argv[2], &dev, false);
		if (!err)
			err = status_phy(bus, dev);
	}

	free(bus);

	return err ? 1 : 0;
}
DEFINE_CMD(status, status_exec);

int list_cb(const char *bus, void *_null)
{
	puts(bus);
	return 0;
}

int list_exec(int argc, char **argv)
{
	const char *match;
	switch (argc) {
	case 1:
		match = "*";
		break;
	case 2:
		match = argv[1];
		break;
	default:
		usage(stderr);
		return 1;
	}

	mdio_for_each(match, list_cb, NULL);
	return 0;
}
DEFINE_CMD(list, list_exec);


int main(int argc, char **argv)
{
	struct cmd *cmd;

	if (mdio_modprobe())
		fprintf(stderr, "WARN: mdio-netlink module not detected, "
			"and could not be loaded.\n");

	if (mdio_init()) {
		fprintf(stderr, "ERROR: Unable to initialize.\n");
		return 1;
	}

	if (argc < 2) {
		usage(stderr);
		return 1;
	}

	for (cmd = &__start_cmds; cmd < &__stop_cmds; cmd++) {
		if (!strcmp(cmd->name, argv[1]))
			return cmd->exec(argc - 1, &argv[1]) ? 1 : 0;
	}

	usage(stderr);
	return 1;
}
