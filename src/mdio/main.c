#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

void usage(FILE *fp)
{
	fputs("Usage: mdio [OBJ] OP\n"
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
	      "    Show this usage message.\n"
	      "\n"
 	      "  OBJ dump [RANGE]\n"
	      "    Dump multiple registers from OBJ. If no range is specified, dump 64\n"
	      "    registers or until the end of the device, whichever comes first.\n"
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
}

int help_exec(int argc, char **argv)
{
	usage(stdout);
	return 0;
}
DEFINE_CMD(help, help_exec);

int main(int argc, char **argv)
{
	struct cmd *cmd;
	char *arg;

	if (mdio_modprobe())
		fprintf(stderr, "WARN: mdio-netlink module not detected, "
			"and could not be loaded.\n");

	if (mdio_init()) {
		fprintf(stderr, "ERROR: Unable to initialize.\n");
		return 1;
	}

	argv_pop(&argc, &argv);

	arg = argv_peek(argc, argv);
	if (!arg) {
		usage(stderr);
		return 1;
	}

	for (cmd = &__start_cmds; cmd < &__stop_cmds; cmd++) {
		if (!strcmp(cmd->name, arg))
			return cmd->exec(argc, argv) ? 1 : 0;
	}

	usage(stderr);
	return 1;
}
