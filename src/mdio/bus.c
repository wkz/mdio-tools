#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

int bus_status_cb(uint32_t *data, int len, int err, void *_null)
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

int bus_status(const char *bus)
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

	err = mdio_xfer(bus, &prog, bus_status_cb, NULL);
	if (err) {
		fprintf(stderr, "ERROR: Unable to read status (%d)\n", err);
		return 1;
	}

	return 0;
}

int bus_list_cb(const char *bus, void *_null)
{
	puts(bus);
	return 0;
}

int bus_exec(int argc, char **argv)
{
	char *arg, *bus;

	argv_pop(&argc, &argv);

	arg = argv_pop(&argc, &argv);
	if (!arg) {
		mdio_for_each("*", bus_list_cb, NULL);
		return 0;
	}

	if (mdio_parse_bus(arg, &bus))
		return 1;

	return bus_status(bus);
}
DEFINE_CMD(bus, bus_exec);
