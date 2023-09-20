#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mdio.h"

int usage(int rc, FILE *fp)
{
	fputs("SYNOPSIS\n"
	      "    mdio            -- List available buses\n"
	      "    mdio BUS        -- Probe BUS for active devices\n"
	      "    mdio BUS OBJ    -- Show status of OBJ\n"
	      "    mdio BUS OBJ OP -- Perform OP on OBJ\n"
	      "\n"
	      "OPTIONS\n"
	      "  -h   This help text\n"
	      "  -v   Show verision and contact information\n"
	      "\n"
	      "Bus names may be abbreviated using glob(3) syntax, i.e. \"fixed*\"\n"
	      "would typically match against \"fixed-0\".\n"
	      "\n"
	      "OBJECTS\n"
	      "  phy PHYAD\n"
	      "    Clause 22 (MDIO) PHY using address PHYAD.\n"
	      "\n"
	      "    REG: u5\n"
	      "\n"
	      "  mmd PRTAD[:DEVAD]\n"
	      "    Clause 45 (XMDIO) PHY using address PRTAD:DEVAD.\n"
	      "\n"
	      "  mmd-c22 PRTAD[:DEVAD]\n"
	      "    Clause 45 (XMDIO) PHY addressed over Clause 22 using address\n"
	      "    PRTAD:DEVAD.\n"
	      "\n"
	      "    REG: u16\n"
	      "\n"
	      "  mva PHYAD\n"
	      "    Operate on Marvell Alaska (mv88e8xxx) PHY using address PHYAD.\n"
	      "    Register 22 is assumed to be the page register.\n"
	      "\n"
	      "    REG: u8|\"copper\"|\"fiber\":u5\n"
	      "\n"
	      "  mvls ID\n"
	      "    Operate on Marvell LinkStreet (mv88e6xxx) device attached to BUS\n"
	      "    using address ID. If ID is 0, single-chip addressing is used; all\n"
	      "    other IDs use multi-chip addressing.\n"
	      "\n"
	      "    REG: u5|\"global1\"|\"global2\" u5\n"
	      "\n"
	      "  xrs PHYAD\n"
	      "    Operate of Arrow/Flexibilis XRS700x device using address PHYAD.\n"
	      "\n"
	      "    REG: u32 (Stride of 2, only even registers are valid)\n"
	      "\n"
	      "OPERATIONS\n"
 	      "  raw REG [DATA[/MASK]]\n"
	      "    Raw register access. Without DATA, REG is read. An unmasked DATA will\n"
	      "    do a single write to REG. DATA with MASK will run the atomic sequence \n"
	      "    write(REG, (read(REG) & MASK) | DATA)\n"
	      "    sequence.\n"
	      "\n"
	      "    DATA: u16\n"
	      "    MASK: u16\n"
	      "\n"
 	      "  bench REG [DATA]\n"
	      "    Benchmark read performance. If DATA is supplied, it is written to REG,\n"
	      "    otherwise the current value in REG is read. REG is then read 1000\n"
	      "    times. Any unexpected values are reported, along with the total time.\n"
	      "\n"
	      "    DATA: u16\n"
	      "\n"
	      "EXAMPLES\n"
	      "  Show all available buses:\n"
	      "     ~# mdio\n"
	      "     30be0000.ethernet-1\n"
	      "     fixed-0\n"
	      "\n"
	      "  List all Clause 22 addressable devices on a bus (using glob(3) pattern\n"
	      "  to abbreviate bus name):\n"
	      "    ~# mdio 3*\n"
	      "    DEV      PHY-ID  LINK\n"
	      "    0x01  0x01410dd0  up\n"
	      "\n"
	      "  Read register 2 from PHY 1:\n"
	      "    ~# mdio 3* phy 1 raw 2\n"
	      "    0x0141\n"
	      "\n"
	      "  Perform a reset on PHY 1:\n"
	      "    ~# mdio 3* phy 1 raw 0 0x8000/0x7fff\n"
	      "\n"
	      "  Read register 0x1000 from MMD 4 on PHY 9:\n"
	      "    ~# mdio 3* mmd 9:4 raw 0x1000\n"
	      "    0x2040\n"
	      "\n"
	      "  Read status register from the copper page of an Alaska PHY:\n"
	      "    ~# mdio 3* mva 1 raw copper:1\n"
	      "    0x796d\n"
	      "\n"
	      "  Set the device number, of LinkStreet switch 4, to 10:\n"
	      "    ~# mdio 3* mvls 4 raw g1:28 0xa/0xfff0\n"
	      , fp);

	return rc;
}

int version(void)
{
	puts("v" PACKAGE_VERSION);
	puts("\nBug report address: " PACKAGE_BUGREPORT);

	return 0;
}

int bus_list_cb(const char *bus, void *_null)
{
	puts(bus);
	return 0;
}

int main(int argc, char **argv)
{
	struct cmd *cmd;
	char *arg, *bus;
	int opt;

	while ((opt = getopt(argc, argv, "hv")) != -1) {
		switch (opt) {
		case 'h':
			return usage(0, stdout);
		case 'v':
			return version();
		default:
			return usage(1, stderr);
		}
	}

	argv += optind;
	argc -= optind;

	if (mdio_init()) {
		if (mdio_modprobe()) {
			fprintf(stderr, "ERROR: mdio-netlink module not "
				"detected, and could not be loaded.\n");
			return 1;
		}

		if (mdio_init()) {
			fprintf(stderr, "ERROR: Unable to initialize.\n");
			return 1;
		}
	}

	arg = argv_pop(&argc, &argv);
	if (!arg)
		return bus_list() ? 1 : 0;

	if (mdio_parse_bus(arg, &bus)) {
		fprintf(stderr, "ERROR: \"%s\" does not match any known bus.\n",
			arg);
		return 1;
	}

	arg = argv_peek(argc, argv);
	if (!arg)
		return bus_status(bus) ? 1 : 0;

	for (cmd = &__start_cmds; cmd < &__stop_cmds; cmd++) {
		if (!strcmp(cmd->name, arg)) {
			argv_pop(&argc, &argv);
			return cmd->exec(bus, argc, argv) ? 1 : 0;
		}
	}

	/* Allow the driver name to be omitted in the common phy/mmd
	 * case. */
	if (strchr(arg, ':'))
		return mmd_exec(bus, argc, argv) ? 1 : 0;
	else
		return phy_exec(bus, argc, argv) ? 1 : 0;
}
