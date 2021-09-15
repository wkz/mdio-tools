#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mdio.h"

int usage(int rc, FILE *fp)
{
	fputs("Usage:\n"
	      "    mdio            -- List available buses\n"
	      "    mdio BUS        -- Probe BUS for active devices\n"
	      "    mdio BUS OBJ    -- Show status of OBJ\n"
	      "    mdio BUS OBJ OP -- Perform OP on OBJ\n"
	      "\n"
	      "Options:\n"
	      "  -h   This help text\n"
	      "  -v   Show verision and contact information\n"
	      "\n"
	      "Bus names may be abbreviated using glob(3) syntax, i.e. \"fixed*\"\n"
	      "would typically match against \"fixed-0\".\n"
	      "\n"
	      "Objects:\n"
	      "  phy PHYAD\n"
	      "    Clause 22 (MDIO) PHY using address PHYAD.\n"
	      "\n"
	      "    REG: u5\n"
	      "\n"
	      "  mmd PRTAD[:DEVAD]\n"
	      "    Clause 45 (XMDIO) PHY using address PRTAD:DEVAD.\n"
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
	      "Operations:\n"
 	      "  raw REG [DATA[/MASK]]\n"
	      "    Raw register access. Without DATA, REG is read. An unmasked DATA will\n"
	      "    do a single write to REG. A masked DATA will perform a read/mask/write\n"
	      "    sequence.\n"
	      "\n"
	      "    DATA: u16\n"
	      "    MASK: u16\n"
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

	if (mdio_modprobe())
		fprintf(stderr, "WARN: mdio-netlink module not detected, "
			"and could not be loaded.\n");

	if (mdio_init()) {
		fprintf(stderr, "ERROR: Unable to initialize.\n");
		return 1;
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
