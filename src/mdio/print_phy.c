#include <stdint.h>
#include <stdio.h>

#include <linux/mdio.h>
#include <net/if.h>

#include "linux/bitfield.h"
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

static const char *get_speed(uint16_t val)
{
	switch (val & MDIO_CTRL1_SPEEDSELEXT) {
	case MDIO_PMA_CTRL1_SPEED1000:
		return "1000";
	case MDIO_PMA_CTRL1_SPEED100:
		return "100";
	case 0:
		return "10";
	}

	switch (val & MDIO_CTRL1_SPEEDSEL) {
	case (MDIO_CTRL1_SPEEDSELEXT | 0x0c):
		return "100g";
	case (MDIO_CTRL1_SPEEDSELEXT | 0x08):
		return "40g";
	case MDIO_CTRL1_SPEED10G:
		return "10g";
	case MDIO_CTRL1_SPEED10P2B:
		return "10-ts/2-tl";
	case MDIO_CTRL1_SPEED2_5G:
		return "2.5g";
	case MDIO_CTRL1_SPEED5G:
		return "5g";
	default:
		return "unknown";
	}
}

void print_phy_bmcr(uint16_t val)
{
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

	printf("  speed: %s-%s\n", get_speed(val),
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

void print_mmd_devid(uint16_t id_hi, uint16_t id_lo)
{
	uint32_t id = (id_hi << 16) | id_lo;

	printf("DEVID(0x02/0x03): %#.8x\n", id);
}

void print_mmd_pkgid(uint16_t id_hi, uint16_t id_lo)
{
	uint32_t id = (id_hi << 16) | id_lo;

	printf("PKGID(0x0E/0x0F): %#.8x\n", id);
}

void print_mmd_devs(uint16_t devs_hi, uint16_t devs_lo)
{
	uint32_t devs = devs_hi << 16 | devs_lo;

	printf("DEVS(0x06/0x05): %#.8x\n", devs);

	fputs("  devices: ", stdout);
	print_bool("vendor2", devs & MDIO_DEVS_VEND2);
	putchar(' ');

	print_bool("vendor1", devs & MDIO_DEVS_VEND1);
	putchar(' ');

	print_bool("c22-ext", devs & MDIO_DEVS_C22EXT);
	putchar(' ');

	print_bool("pma4", devs & MDIO_DEVS_PRESENT(11));
	putchar(' ');

	print_bool("pma3", devs & MDIO_DEVS_PRESENT(10));
	putchar(' ');

	print_bool("pma2", devs & MDIO_DEVS_PRESENT(9));
	putchar(' ');

	print_bool("pma1", devs & MDIO_DEVS_PRESENT(8));
	fputs("\n"
	      "           ", stdout);

	print_bool("aneg", devs & MDIO_DEVS_AN);
	putchar(' ');

	print_bool("tc", devs & MDIO_DEVS_TC);
	putchar(' ');

	print_bool("dte-xs", devs & MDIO_DEVS_DTEXS);
	putchar(' ');

	print_bool("phy-xs", devs & MDIO_DEVS_PHYXS);
	putchar(' ');

	print_bool("pcs", devs & MDIO_DEVS_PCS);
	putchar(' ');

	print_bool("wis", devs & MDIO_DEVS_WIS);
	putchar(' ');

	print_bool("pma/pmd", devs & MDIO_DEVS_PMAPMD);
	putchar(' ');

	print_bool("c22", devs & MDIO_DEVS_C22PRESENT);
	putchar('\n');
}

static void print_pma_ctrl1(uint16_t val)
{
	printf("CTRL1(0x00): %#.4x\n", val);

	fputs("  flags: ", stdout);
	print_bool("reset", val & MDIO_CTRL1_RESET);
	putchar(' ');

	print_bool("low-power", val & MDIO_CTRL1_LPOWER);
	putchar(' ');

	print_bool("remote-loopback", val & BIT(1));
	putchar(' ');

	print_bool("local-loopback", val & MDIO_PMA_CTRL1_LOOPBACK);
	putchar('\n');

	printf("  speed: %s\n", get_speed(val));
}

static void print_pma_stat1(uint16_t val)
{
	printf("STAT1(0x01): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("pias", val & BIT(9));
	putchar(' ');

	print_bool("peas", val & BIT(8));
	putchar(' ');

	print_bool("low-power", val & MDIO_STAT1_LPOWERABLE);
	putchar('\n');

	fputs("  flags:        ", stdout);
	print_bool("fault", val & MDIO_STAT1_FAULT);
	putchar(' ');

	print_bool("link", val & MDIO_STAT1_LSTATUS);
	putchar('\n');
}

static void print_pma_speed(uint16_t val)
{
	printf("SPEED(0x04): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("100g", val & BIT(9));
	putchar(' ');

	print_bool("40g", val & BIT(8));
	putchar(' ');

	print_bool("10g/1g", val & BIT(7));
	putchar(' ');

	print_bool("10", val & MDIO_PMA_SPEED_10);
	putchar(' ');

	print_bool("100", val & MDIO_PMA_SPEED_100);
	putchar(' ');

	print_bool("1000", val & MDIO_PMA_SPEED_1000);
	putchar(' ');

	print_bool("10-ts", val & MDIO_PMA_SPEED_10P);
	putchar(' ');

	print_bool("2-tl", val & MDIO_PMA_SPEED_2B);
	putchar(' ');

	print_bool("10g", val & MDIO_SPEED_10G);
	putchar('\n');
}

static void print_pma_ctrl2(uint16_t val)
{
	const char *type;
	static const char *const pma_type[0x80] = {
		[MDIO_PMA_CTRL2_5GBT]		= "2.5g-t",
		[MDIO_PMA_CTRL2_2_5GBT]		= "2.25g-t",
		/* TODO: the many, many 40G and 100G types... */
		[MDIO_PMA_CTRL2_10BT]		= "10-t",
		[MDIO_PMA_CTRL2_100BTX]		= "100-tx",
		[MDIO_PMA_CTRL2_1000BKX]	= "1000-kx",
		[MDIO_PMA_CTRL2_1000BT]		= "1000-t",
		[MDIO_PMA_CTRL2_10GBKR]		= "10g-kr",
		[MDIO_PMA_CTRL2_10GBKX4]	= "10g-kx4",
		[MDIO_PMA_CTRL2_10GBT]		= "10g-t",
		[MDIO_PMA_CTRL2_10GBLRM]	= "10g-lrm",
		[MDIO_PMA_CTRL2_10GBSR]		= "10g-sr",
		[MDIO_PMA_CTRL2_10GBLR]		= "10g-lr",
		[MDIO_PMA_CTRL2_10GBER]		= "10g-er",
		[MDIO_PMA_CTRL2_10GBLX4]	= "10g-lx4",
		[MDIO_PMA_CTRL2_10GBSW]		= "10g-sw",
		[MDIO_PMA_CTRL2_10GBLW]		= "10g-lw",
		[MDIO_PMA_CTRL2_10GBEW]		= "10g-ew",
		[MDIO_PMA_CTRL2_10GBCX4]	= "10g-cx4",
	};

	printf("CTRL2(0x07): %#.4x\n", val);

	fputs("  flags: ", stdout);
	print_bool("pias", val & BIT(9));
	putchar(' ');

	print_bool("peas", val & BIT(8));
	putchar('\n');

	type = pma_type[FIELD_GET(MDIO_PMA_CTRL2_TYPE, val)];
	printf("  type:  %s\n", type ?: "unknown");
}

static void print_mmd_stat2_flags(uint16_t val)
{
	fputs("  flags:        ", stdout);
	print_bool("present", (val & MDIO_STAT2_DEVPRST) ==
			      MDIO_STAT2_DEVPRST_VAL);
	putchar(' ');

	print_bool("tx-fault", val & MDIO_STAT2_TXFAULT);
	putchar(' ');

	print_bool("rx-fault", val & MDIO_STAT2_RXFAULT);
	putchar('\n');
}

static void print_pma_stat2(uint16_t val)
{
	printf("STAT2(0x08): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("tx-fault", val & MDIO_PMA_STAT2_TXFLTABLE);
	putchar(' ');

	print_bool("rx-fault", val & MDIO_PMA_STAT2_RXFLTABLE);
	putchar(' ');

	print_bool("ext-register", val & MDIO_PMA_STAT2_EXTABLE);
	putchar(' ');

	print_bool("tx-disable", val & MDIO_PMD_STAT2_TXDISAB);
	putchar(' ');

	print_bool("local-loopback", val & MDIO_PMA_STAT2_LBABLE);
	fputs("\n"
	      "                ", stdout);

	print_bool("10g-sr", val & MDIO_PMA_STAT2_10GBSR);
	putchar(' ');

	print_bool("10g-lr", val & MDIO_PMA_STAT2_10GBLR);
	putchar(' ');

	print_bool("10g-er", val & MDIO_PMA_STAT2_10GBER);
	putchar(' ');

	print_bool("10g-lx4", val & MDIO_PMA_STAT2_10GBLX4);
	putchar(' ');

	print_bool("10g-sw", val & MDIO_PMA_STAT2_10GBSW);
	putchar(' ');

	print_bool("10g-lw", val & MDIO_PMA_STAT2_10GBLW);
	putchar(' ');

	print_bool("10g-ew", val & MDIO_PMA_STAT2_10GBEW);
	putchar('\n');

	print_mmd_stat2_flags(val);
}

static void print_pma_extable(uint16_t val)
{
	printf("EXTABLE(0x0B): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("10g-cx4", val & MDIO_PMA_EXTABLE_10GCX4);
	putchar(' ');

	print_bool("10g-lrm", val & MDIO_PMA_EXTABLE_10GBLRM);
	putchar(' ');

	print_bool("10g-t", val & MDIO_PMA_EXTABLE_10GBT);
	putchar(' ');

	print_bool("10g-kx4", val & MDIO_PMA_EXTABLE_10GBKX4);
	putchar(' ');

	print_bool("10g-kr", val & MDIO_PMA_EXTABLE_10GBKR);
	putchar(' ');

	print_bool("1000-t", val & MDIO_PMA_EXTABLE_1000BT);
	fputs("\n"
	      "                ", stdout);

	print_bool("1000-kx", val & MDIO_PMA_EXTABLE_1000BKX);
	putchar(' ');

	print_bool("100-tx", val & MDIO_PMA_EXTABLE_100BTX);
	putchar(' ');

	print_bool("10-t", val & MDIO_PMA_EXTABLE_10BT);
	putchar(' ');

	print_bool("2.5g/5g-t", val & MDIO_PMA_EXTABLE_NBT);
	putchar('\n');
}

static void print_pma_extra(uint32_t *data)
{
	print_pma_ctrl2(data[7]);
	putchar('\n');
	print_pma_stat2(data[8]);

	if (data[8] & MDIO_PMA_STAT2_EXTABLE) {
		putchar('\n');
		print_pma_extable(data[11]);
	}
}

const struct mmd_print_device pma_print_device = {
	.print_ctrl1 = print_pma_ctrl1,
	.print_stat1 = print_pma_stat1,
	.print_speed = print_pma_speed,
	.print_extra = print_pma_extra,
};

static void print_pcs_ctrl1(uint16_t val)
{
	printf("CTRL1(0x00): %#.4x\n", val);

	fputs("  flags: ", stdout);
	print_bool("reset", val & MDIO_CTRL1_RESET);
	putchar(' ');

	print_bool("loopback", val & MDIO_PCS_CTRL1_LOOPBACK);
	putchar(' ');

	print_bool("low-power", val & MDIO_CTRL1_LPOWER);
	putchar(' ');

	print_bool("lpi-clock-stop", val & MDIO_PCS_CTRL1_CLKSTOP_EN);
	putchar(' ');

	putchar('\n');

	printf("  speed: %s\n", get_speed(val));
}

static void print_pcs_stat1(uint16_t val)
{
	printf("STAT1(0x01): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("lpi-clock-stop", val & BIT(6));
	putchar(' ');

	print_bool("low-power", val & MDIO_STAT1_LPOWERABLE);
	putchar('\n');

	fputs("  flags:        ", stdout);
	print_bool("rx-lpi-recv", val & BIT(11));
	putchar(' ');

	print_bool("tx-lpi-recv", val & BIT(10));
	putchar(' ');

	print_bool("rx-lpi-ind", val & BIT(9));
	putchar(' ');

	print_bool("tx-lpi-ind", val & BIT(8));
	putchar(' ');

	print_bool("fault", val & MDIO_STAT1_FAULT);
	putchar(' ');

	print_bool("link", val & MDIO_STAT1_LSTATUS);
	putchar('\n');
}

static void print_pcs_speed(uint16_t val)
{
	printf("SPEED(0x04): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("100g", val & BIT(3));
	putchar(' ');

	print_bool("40g", val & BIT(2));
	putchar(' ');

	print_bool("10-ts/2-tl", val & MDIO_PCS_SPEED_10P2B);
	putchar(' ');

	print_bool("10g", val & MDIO_SPEED_10G);
	putchar('\n');
}

static void print_pcs_ctrl2(uint16_t val)
{
	const char *type;
	static const char *const pcs_type[0x10] = {
		[0x5]			= "100g-r",
		[0x4]			= "40g-r",
		[MDIO_PCS_CTRL2_10GBT]	= "10g-t",
		[MDIO_PCS_CTRL2_10GBW]	= "10g-w",
		[MDIO_PCS_CTRL2_10GBX]	= "10g-x",
		[MDIO_PCS_CTRL2_10GBR]	= "10g-r",
	};

	printf("CTRL2(0x07): %#.4x\n", val);

	type = pcs_type[FIELD_GET(MDIO_PCS_CTRL2_TYPE, val)];
	printf("  type: %s\n", type ?: "unknown");
}

static void print_pcs_stat2(uint16_t val)
{
	printf("STAT2(0x08): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("100g-r", val & BIT(5));
	putchar(' ');

	print_bool("40g-r", val & BIT(4));
	putchar(' ');

	print_bool("10g-t", val & BIT(3));
	putchar(' ');

	print_bool("10g-w", val & MDIO_PCS_STAT2_10GBW);
	putchar(' ');

	print_bool("10g-x", val & MDIO_PCS_STAT2_10GBX);
	putchar(' ');

	print_bool("10g-r", val & MDIO_PCS_STAT2_10GBR);
	putchar('\n');

	print_mmd_stat2_flags(val);
}

static void print_pcs_extra(uint32_t *data)
{
	print_pcs_ctrl2(data[7]);
	putchar('\n');
	print_pcs_stat2(data[8]);
}

const struct mmd_print_device pcs_print_device = {
	.print_ctrl1 = print_pcs_ctrl1,
	.print_stat1 = print_pcs_stat1,
	.print_speed = print_pcs_speed,
	.print_extra = print_pcs_extra,
};

static void print_an_ctrl1(uint16_t val)
{
	printf("CTRL1(0x00): %#.4x\n", val);

	fputs("  flags: ", stdout);
	print_bool("reset", val & MDIO_CTRL1_RESET);
	putchar(' ');

	print_bool("ext-page", val & MDIO_AN_CTRL1_XNP);
	putchar(' ');

	print_bool("aneg-enable", val & MDIO_AN_CTRL1_ENABLE);
	putchar(' ');

	print_bool("aneg-restart", val & MDIO_AN_CTRL1_RESTART);
	putchar('\n');
}

static void print_an_stat1(uint16_t val)
{
	printf("STAT1(0x01): %#.4x\n", val);

	fputs("  capabilities: ", stdout);
	print_bool("aneg-capable", val & MDIO_AN_STAT1_ABLE);
	putchar(' ');

	print_bool("partner-capable", val & MDIO_AN_STAT1_LPABLE);
	putchar('\n');

	fputs("  flags:        ", stdout);
	print_bool("ext-page", val & MDIO_AN_STAT1_XNP);
	putchar(' ');

	print_bool("parallel-fault", val & MDIO_STAT1_FAULT);
	putchar(' ');

	print_bool("page", val & MDIO_AN_STAT1_PAGE);
	putchar(' ');

	print_bool("aneg-complete", val & MDIO_AN_STAT1_COMPLETE);
	putchar(' ');

	print_bool("remote-fault", val & MDIO_AN_STAT1_RFAULT);
	fputs("\n"
	      "                ", stdout);

	print_bool("link", val & MDIO_STAT1_LSTATUS);
	putchar('\n');
}

const struct mmd_print_device an_print_device = {
	.print_ctrl1 = print_an_ctrl1,
	.print_stat1 = print_an_stat1,
};
