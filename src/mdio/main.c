#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mdio.h"

int usage(int rc, FILE *fp)
{
	fputs("Usage: mdio [OPT] [OBJ] OP\n"
	      "\n"
	      "Options:\n"
	      "  -h   This help text\n"
	      "  -v   Show verision and contact information\n"
	      "\n"
	      "Operations:\n"
	      "\n"
	      "  bus [BUS]\n"
	      "    If BUS is specified, scan the bus and show all attached PHYs.\n"
	      "    If BUS is omitted, list all buses on the system. Uses glob(3)\n"
	      "    matching to locate bus, i.e. \"fixed*\" would typically match\n"
	      "    against \"fixed-0\".\n"
	      "\n"
	      "  help\n"
	      "    Show this help text.\n"
	      "\n"
	      "  version\n"
	      "    Show version and contact information.\n"
	      "\n"
 	      "  OBJ raw REG [VAL[/MASK]]\n"
	      "    Raw register access. Without VAL, REG is read. An unmasked VAL will\n"
	      "    do a single write to REG. A masked VAL will perform a read/mask/write\n"
	      "    sequence.\n"
	      "\n"
	      "Objects:\n"
	      "\n"
	      "  phy BUS PORT[:DEV]\n"
	      "    Operate on standard PHY attached to BUS using either Clause 22 (PORT)\n"
	      "    or Clause 45 (PORT:DEV) addressing.\n"
	      "\n"
	      "    REG: u5|u16 (Clause 22, Clause 45)\n"
	      "    VAL: u16\n"
	      "\n"
	      "  mva BUS PORT\n"
	      "    Operate on Marvell Alaska (mv88e8xxx) PHY attached to BUS\n"
	      "    using address PORT. Register 22 is assumed to be the page register.\n"
	      "\n"
	      "    REG: u8|\"copper\"|\"fiber\" u5\n"
	      "    VAL: u16\n"
	      "\n"
	      "  mvls BUS ID\n"
	      "    Operate on Marvell LinkStreet (mv88e6xxx) device attached to BUS\n"
	      "    using address ID. If ID is 0, single-chip addressing is used; all\n"
	      "    other IDs use multi-chip addressing.\n"
	      "\n"
	      "    REG: u5|\"global1\"|\"global2\" u5\n"
	      "    VAL: u16\n"
	      "\n"
	      "  xrs BUS ID\n"
	      "    Operate of Arrow/Flexibilis XRS700x device attached to BUS using\n"
	      "    address ID.\n"
	      "\n"
	      "    REG: u32 (Stride of 2, only even registers are valid)\n"
	      "    VAL: u16\n"
	      , fp);

	return rc;
}

int version(void)
{
	puts("v" PACKAGE_VERSION);
	puts("\nBug report address: " PACKAGE_BUGREPORT);

	return 0;
}

int main(int argc, char **argv)
{
	struct cmd *cmd;
	char *arg;
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

	arg = argv_peek(argc, argv);
	if (!arg)
		return usage(1, stderr);

	if (mdio_modprobe())
		fprintf(stderr, "WARN: mdio-netlink module not detected, "
			"and could not be loaded.\n");

	if (mdio_init()) {
		fprintf(stderr, "ERROR: Unable to initialize.\n");
		return 1;
	}

	for (cmd = &__start_cmds; cmd < &__stop_cmds; cmd++) {
		if (!strcmp(cmd->name, arg))
			return cmd->exec(argc, argv) ? 1 : 0;
	}

	return usage(1, stderr);
}
