#include <stdint.h>
#include <stdio.h>

#include <linux/mdio.h>
#include <net/if.h>

#include "mdio.h"

void print_bool(const char *name, int on)
{
	if (on)
		fputs("\e[1m+", stdout);
	else
		fputs("-", stdout);

	fputs(name, stdout);

	if (on)
		fputs("\e[0m", stdout);
}

void print_phy_bmcr(uint16_t val)
{
	int speed = 10;

	if (val & BMCR_SPEED100)
		speed = 100;
	if (val & BMCR_SPEED1000)
		speed = 1000;

	printf("BMCR(0x00): %#.4x\n", val);

	fputs("  flags: ", stdout);
	print_bool("reset", val & BMCR_RESET);
	putchar(' ');

	print_bool("loopback", val & BMCR_LOOPBACK);
	putchar(' ');

	print_bool("aneg-enable", val & BMCR_ANENABLE);
	putchar(' ');

	print_bool("power-down", val & BMCR_PDOWN);
	putchar(' ');

	print_bool("isolate", val & BMCR_ISOLATE);
	putchar(' ');

	print_bool("aneg-restart", val & BMCR_ANRESTART);
	fputs("\n"
	      "         ", stdout);

	print_bool("collision-test", val & BMCR_CTST);
	putchar('\n');

	printf("  speed: %d-%s\n", speed,
	       (val & BMCR_FULLDPLX) ? "full" : "half");
}

void print_phy_bmsr(uint16_t val)
{
	printf("BMSR(0x01): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("100-t4", val & BMSR_100BASE4);
	putchar(' ');

	print_bool("100-tx-f", val & BMSR_100FULL);
	putchar(' ');

	print_bool("100-tx-h", val & BMSR_100HALF);
	putchar(' ');

	print_bool("10-t-f", val & BMSR_10FULL);
	putchar(' ');

	print_bool("10-t-h", val & BMSR_10HALF);
	putchar(' ');

	print_bool("100-t2-f", val & BMSR_100FULL2);
	putchar(' ');

	print_bool("100-t2-h", val & BMSR_100HALF2);
	putchar('\n');

	fputs(  "  flags:        ", stdout);
	print_bool("ext-status", val & BMSR_ESTATEN);
	putchar(' ');

	print_bool("aneg-complete", val & BMSR_ANEGCOMPLETE);
	putchar(' ');

	print_bool("remote-fault", val & BMSR_RFAULT);
	putchar(' ');

	print_bool("aneg-capable", val & BMSR_ANEGCAPABLE);
	putchar(' ');

	print_bool("link", val & BMSR_LSTATUS);
	fputs("\n"
	      "                ", stdout);

	print_bool("jabber", val & BMSR_JCD);
	putchar(' ');

	print_bool("ext-register", val & BMSR_ERCAP);
	putchar('\n');
}

void print_phy_id(uint16_t id_hi, uint16_t id_lo)
{
	uint32_t id = (id_hi << 16) | id_lo;

	printf("ID(0x02/0x03): %#.8x\n", id);
}

void print_phy_estatus(uint16_t val)
{
	printf("ESTATUS(0x0F): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("1000-x-f", val & ESTATUS_1000_XFULL);
	putchar(' ');

	print_bool("1000-x-h", val & ESTATUS_1000_XHALF);
	putchar(' ');

	print_bool("1000-t-f", val & ESTATUS_1000_TFULL);
	putchar(' ');

	print_bool("1000-t-h", val & ESTATUS_1000_THALF);
	putchar('\n');
}
