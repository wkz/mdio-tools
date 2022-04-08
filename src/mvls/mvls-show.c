#include "config.h"

#include <err.h>
#include <stdio.h>
#include <string.h>

#include "mvls.h"

static char prio_c(struct prio_override prio)
{
	if (!prio.set)
		return '-';

	return '0' + prio.pri;
}

const char *atu_uc_str[] = {
	"UNUSED", "age 1",     "age 2",  "age 3",
	"age 4",  "age 5"  ,   "age 6",  "age 7",
	"policy", "policy-po", "nrl",    "nrl-po",
	"mgmt",   "mgmt-po",   "static", "static-po"
};

const char *atu_mc_str[] = {
	"UNUSED",   "resvd1",  "resvd2",  "resvd3",
	"policy",   "nrl",     "mgmt",    "static",
	"resvd8",   "resvd9",  "resvda",  "resvdb",
	"policy-po", "nrl-po", "mgmt-po", "static-po"
};

char port_fmode_c[] = { 'n', 'D', 'P', 'E' };
char port_emode_c[] = { '=', 'u', 't', 'D' };
char port_state_c[] = { '-', 'B', 'L', 'f' };
char port_qmode_c[] = { '-', 'F', 'C', 's' };

static const char *port_link_str(struct port *port)
{
	uint16_t stat = reg16(port->regs, 0);

	if (!bit(stat, 11))
		return "-";

	switch (bits(stat, 8, 2)) {
	case 0:
		return bit(stat, 10) ? "10" : "10h";
	case 1:
		return bit(stat, 10) ? "100" : "100h";
	case 2:
		return bit(stat, 10) ? "1G" : "1Gh";
	case 3:
		return bit(stat, 10) ? "XG" : "XGh";
	}

	return "NONE";
}

static const char *port_lag_str(struct port *port)
{
	static char str[] = "31";

	int lag = port_op(port, lag);

	if (lag < 0)
		return "-";

	snprintf(str, sizeof(str), "%d", lag & 0x1f);
	return str;
}

static void dev_print_portvec(struct dev *dev, uint16_t portvec)
{
	int i;

	for (i = 0; i < 11; i++) {
		if (bit(portvec, i))
			printf("  %x", i);
		else
			fputs(i >= dev->chip->n_ports ?
			      "   " : "  .", stdout);
	}

}

static void dev_show_pvt(struct dev *dev)
{
	struct port *port;
	int di, pi, err;

	puts("\e[7m D P  0  1  2  3  4  5  6  7  8  9  a\e[0m");

	TAILQ_FOREACH(port, &dev->ports, node) {
		err = port_load_regs(port);
		if (err) {
			warn("failed querying ports");
			return;
		}
		printf("%2x %x", dev->index, port->index);
		dev_print_portvec(dev, bits(reg16(port->regs, 6), 0, 11));
		putchar('\n');
	}

	err = dev_load_pvt(dev);
	if (err) {
		warn("failed querying pvt");
		return;
	}

	for (di = 0; di < 32; di++) {
		for (pi = 0; pi < 16; pi++) {
			uint16_t portvec = reg16(dev->pvt, (di << 4) | pi);

			if (portvec) {
				printf("%2x %x", di, pi);
				dev_print_portvec(dev, bits(portvec, 0, 11));
				putchar('\n');
			}
		}
	}
}

void env_show_ports(struct env *env)
{
	uint16_t ctrl, ctrl2;
	struct port *port;
	struct dev *dev;

	puts("\e[7mNETDEV    P  LINK  MO  FL  S  L  .1Q  PVID   FID\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (env->multichip)
			printf("\e[2m\e[7mDEV:%x %-42s\e[0m\n",
			       dev->index, dev->chip->id);

		TAILQ_FOREACH(port, &dev->ports, node) {
			port_load_regs(port);
			ctrl = reg16(port->regs, 4);
			ctrl2 = reg16(port->regs, 8);

			printf("%-8s  %x  %4s  %c%c  %c%c  %c %2s  %c%c%c  %4u  %4u%s\n", port->netdev,
			       port->index,
			       port_link_str(port),
			       port_fmode_c[bits(ctrl,  8, 2)],
			       port_emode_c[bits(ctrl, 12, 2)],
			       bit(ctrl, 2) ? 'u' : '-',
			       bit(ctrl, 3) ? 'm' : '-',
			       port_state_c[bits(ctrl, 0, 2)],
			       port_lag_str(port),
			       port_qmode_c[bits(ctrl2, 10, 2)],
			       bit(ctrl2, 8) ? '-' : 'u',
			       bit(ctrl2, 9) ? '-' : 't',
			       bits(reg16(port->regs, 7), 0, 12),
			       port_op(port, fid),
			       bit(ctrl2, 7) ? "" : "!map"
				);
		}
	}
}

void env_show_atu(struct env *env)
{
	struct dev *dev;
	struct devlink_region atu = { 0 };
	struct mv88e6xxx_devlink_atu_entry *kentry;
	struct atu_entry entry;
	int err;

	puts("\e[7mADDRESS             FID  STATE      Q  F  0  1  2  3  4  5  6  7  8  9  a\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (env->multichip)
			printf("\e[2m\e[7mDEV:%x %-67s\e[0m\n",
			       dev->index, dev->chip->id);

		err = devlink_region_get(&env->dl, &dev->devlink, "atu",
					 devlink_region_dup_cb, &atu);
		if (err) {
			warn("failed querying atu");
			break;
		}

		kentry = (void *)atu.data.u8;
		while (!dev_op(dev, atu_parse, kentry, &entry)) {
			printf("%02x:%02x:%02x:%02x:%02x:%02x  %4u  %-9s  %c  %c",
			       entry.addr[0], entry.addr[1], entry.addr[2],
			       entry.addr[3], entry.addr[4], entry.addr[5],
			       entry.fid, (entry.addr[0] & 1) ?
			       atu_mc_str[entry.state.mc] : atu_uc_str[entry.state.uc],
			       prio_c(entry.qpri), prio_c(entry.fpri));

			if (entry.lag) {
				printf("  lag %x", entry.portvec);
				goto next;
			}

			dev_print_portvec(dev, entry.portvec);

		next:
			putchar('\n');
			kentry++;
		}

		devlink_region_free(&atu);
	}
}

void env_show_vtu(struct env *env)
{
	struct dev *dev;
	struct devlink_region vtu = { 0 };
	struct mv88e6xxx_devlink_vtu_entry *kentry;
	struct vtu_entry entry;
	int err, i;

	puts("\e[7m VID   FID  SID  P  Q  F  0  1  2  3  4  5  6  7  8  9  a\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (env->multichip)
			printf("\e[2m\e[7mDEV:%x %-51s\e[0m\n",
			       dev->index, dev->chip->id);

		err = devlink_region_get(&env->dl, &dev->devlink, "vtu",
					 devlink_region_dup_cb, &vtu);
		if (err) {
			warn("failed querying vtu");
			break;
		}

		kentry = (void *)vtu.data.u8;
		while (!dev_op(dev, vtu_parse, kentry, &entry)) {
			printf("%4u  %4u  %3u  %c  %c  %c",
			       entry.vid, entry.fid, entry.sid,
			       entry.policy ? 'y' : '-',
			       prio_c(entry.qpri), prio_c(entry.fpri));

			for (i = 0; i < 11; i++) {
				switch (entry.member[i]) {
				case VTU_UNMODIFIED:
					fputs(i >= dev->chip->n_ports ? "   " : "  =", stdout);
					break;
				case VTU_UNTAGGED:
					fputs("  u", stdout);
					break;
				case VTU_TAGGED:
					fputs("  t", stdout);
					break;
				case VTU_NOT_MEMBER:
					fputs("  .", stdout);
					break;
				}
			}

			putchar('\n');
			kentry++;
		}

		devlink_region_free(&vtu);
	}
}

void env_show_stu(struct env *env)
{
	struct dev *dev;
	struct devlink_region stu = { 0 };
	struct mv88e6xxx_devlink_stu_entry *kentry;
	struct stu_entry entry;
	int err, i;

	puts("\e[7mSID  0  1  2  3  4  5  6  7  8  9  a\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (env->multichip)
			printf("\e[2m\e[7mDEV:%x %-30s\e[0m\n",
			       dev->index, dev->chip->id);

		err = devlink_region_get(&env->dl, &dev->devlink, "stu",
					 devlink_region_dup_cb, &stu);
		if (err) {
			warn("failed querying stu");
			break;
		}

		kentry = (void *)stu.data.u8;
		while (!dev_op(dev, stu_parse, kentry, &entry)) {
			printf("%3u", entry.sid);

			for (i = 0; i < 11; i++) {
				switch (entry.state[i]) {
				case STU_DISABLED:
					fputs(i >= dev->chip->n_ports ? "   " : "  -", stdout);
					break;
				default:
					printf("  %c", port_state_c[entry.state[i]]);
				}
			}

			putchar('\n');
			kentry++;
		}

		devlink_region_free(&stu);
	}
}

void pvt_print_cell(uint16_t spvt, uint16_t dpvt, int src, int dst)
{
	if (bit(spvt, dst) && bit(dpvt, src))
		fputs(" x", stdout);
	else if (bit(spvt, dst))
		fputs(" ^", stdout);
	else if (bit(dpvt, src))
		fputs(" <", stdout);
	else
		fputs(" .", stdout);

}

void env_show_pvt_port(struct port *src, unsigned lags)
{
	struct env *env = src->dev->env;
	uint16_t spvt, dpvt;
	struct port *port;
	struct dev *dev;
	int lag;

	printf("\e[7m%x %x\e[0m", src->dev->index, src->index);

	TAILQ_FOREACH(dev, &env->devs, node) {
		TAILQ_FOREACH(port, &dev->ports, node) {
			if (port->dev == src->dev) {
				spvt = reg16(src->regs, 6);
				dpvt = reg16(port->regs, 6);
			} else {
				spvt = reg16(dev->pvt, (src->dev->index << 4) + src->index);
				dpvt = reg16(src->dev->pvt, (port->dev->index << 4) + port->index);
			}

			pvt_print_cell(spvt, dpvt, src->index, port->index);
		}

		if (TAILQ_NEXT(dev, node) || lags)
			putchar(' ');
	}

	for (lag = 0; lag < 16; lag++) {
		if (!(lags & (1 << lag)))
			continue;

		dpvt = reg16(src->dev->pvt, (0x1f << 4) + lag);
		pvt_print_cell(0, dpvt, src->index, 0);
	}

	putchar('\n');
}

void env_show_pvt(struct env *env)
{
	struct dev *dev;
	struct port *port;
	unsigned lags = 0;
	int err, lag;

	fputs("\e[7mD  ", stdout);
	TAILQ_FOREACH(dev, &env->devs, node) {
		err = dev_load_pvt(dev);
		if (err) {
			warn("failed querying pvt");
			return;
		}

		TAILQ_FOREACH(port, &dev->ports, node) {
			err = port_load_regs(port);
			if (err) {
				warn("failed querying ports");
				return;
			}

			lag = port_op(port, lag);
			if (lag >= 0)
				lags |= 1 << lag;

			printf(" %x", dev->index);
		}

		if (TAILQ_NEXT(dev, node))
			putchar(' ');
	}

	if (lags)
		putchar(' ');

	for (lag = 0; lag < 16; lag++)
		if (lags & (1 << lag))
			fputs(" L", stdout);

	fputs("\n  P", stdout);
	TAILQ_FOREACH(dev, &env->devs, node) {
		TAILQ_FOREACH(port, &dev->ports, node)
			printf(" %x", port->index);

		if (TAILQ_NEXT(dev, node) || lags)
			putchar(' ');
	}

	for (lag = 0; lag < 16; lag++)
		if (lags & (1 << lag))
			printf(" %x", lag);

	puts("\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		TAILQ_FOREACH(port, &dev->ports, node) {
			env_show_pvt_port(port, lags);
		}

		if (TAILQ_NEXT(dev, node))
			puts("\e[7m   \e[0m");
	}
}

void show_join(void)
{
	putchar('\n');
}

const struct printer printer_show = {
	.env_print_ports = env_show_ports,
	.env_print_vtu = env_show_vtu,
	.env_print_atu = env_show_atu,
	.env_print_stu = env_show_stu,
	.env_print_pvt = env_show_pvt,
	.dev_print_pvt = dev_show_pvt,

	.join = show_join,
};
