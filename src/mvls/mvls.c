#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devlink.h"
#include "mvls.h"

#define reg16(_region, _reg) ((_region).data.u16[_reg])
#define bits(_reg, _bit, _n) (((_reg) >> (_bit)) & ((1 << (_n)) - 1))
#define bit(_reg, _bit) bits(_reg, _bit, 1)

static int opal_dev_vtu_parse(struct dev *dev,
			      struct mv88e6xxx_devlink_vtu_entry *kentry,
			      struct vtu_entry *entry)
{
	int i;

	if (!bit(kentry->vid, 12))
		return ENODATA;

	entry->vid = bits(kentry->vid, 0, 12);
	entry->fid = bits(kentry->fid, 0, 12);
	entry->sid = bits(kentry->sid, 0, 6);

	for (i = 0; i < 11; i++)
		entry->member[i] =
			(kentry->data[i >> 2] >> ((i & 3) << 2)) & 3;

	entry->policy = bit(kentry->fid, 12);

	/* TODO */
	entry->qpri.set = 0;
	entry->fpri.set = 0;
	return 0;
}

int opal_dev_atu_parse(struct dev *dev,
		       struct mv88e6xxx_devlink_atu_entry *kentry,
		       struct atu_entry *entry)
{
	if (!bits(kentry->atu_data, 0, 4))
		return ENODATA;

	entry->fid = kentry->fid;
	entry->addr[0] = bits(kentry->atu_01, 8, 8);
	entry->addr[1] = bits(kentry->atu_01, 0, 8);
	entry->addr[2] = bits(kentry->atu_23, 8, 8);
	entry->addr[3] = bits(kentry->atu_23, 0, 8);
	entry->addr[4] = bits(kentry->atu_45, 8, 8);
	entry->addr[5] = bits(kentry->atu_45, 0, 8);

	/* TODO */
	entry->qpri.set = 0;
	entry->fpri.set = 0;

	entry->lag = bit(kentry->atu_data, 15);
	entry->portvec = bits(kentry->atu_data, 4, 11);
	entry->state.uc = bits(kentry->atu_data, 0, 4);
	return 0;
}

int opal_port_lag(struct port *port)
{
	uint16_t ctrl1 = reg16(port->regs, 5);

	if (!bit(ctrl1, 14))
		return -1;

	return bits(ctrl1, 8, 4);
}

uint16_t opal_port_fid(struct port *port)
{
	return bits(reg16(port->regs, 6), 12, 4) |
		(bits(reg16(port->regs, 5), 0, 8) << 4);
}

const struct family opal_family = {
	.port_lag = opal_port_lag,
	.port_fid = opal_port_fid,

	.dev_atu_parse = opal_dev_atu_parse,
	.dev_vtu_parse = opal_dev_vtu_parse,
};

const struct chip chips[] = {
	{
		.id = "Marvell 88E6097/88E6097F",
		.family = &opal_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6352",
		.family = &opal_family,
		.n_ports = 7,
	},
	{
		.id = "Marvell 88E6390X",
		.family = &opal_family,
		.n_ports = 11,
	},

	{ .id = NULL }
};

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


static int port_init(struct port *port)
{
	struct dev *dev = port->dev;
	struct env *env = dev->env;

	return devlink_port_region_get(&env->dl, &dev->devlink, port->index,
				       "port", devlink_region_dup_cb, &port->regs);
}

static int dev_init(struct dev *dev)
{
	int err;

	err = devlink_region_get(&dev->env->dl, &dev->devlink,
				 "global1", devlink_region_dup_cb, &dev->global1);
	if (err)
		return err;

	dev->index = bits(reg16(dev->global1, 0x1c), 0, 5);
	return 0;
}




static struct dev *env_dev_find(struct env *env,
				const char *busid, const char *devid)
{
	struct dev *dev;

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (strcmp(dev->devlink.bus, busid))
			continue;

		if (strcmp(dev->devlink.dev, devid))
			continue;

		return dev;
	}

	return NULL;
}

static int env_init_port_cb(const struct nlmsghdr *nlh, void *_env)
{
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	struct env *env = _env;
	struct port *port;
	struct dev *dev;

	devlink_parse(nlh, tb);
	if (!tb[DEVLINK_ATTR_BUS_NAME]   ||
	    !tb[DEVLINK_ATTR_DEV_NAME]   ||
	    !tb[DEVLINK_ATTR_PORT_INDEX] ||
	    !tb[DEVLINK_ATTR_PORT_FLAVOUR])
		return MNL_CB_ERROR;

	dev = env_dev_find(env,
			   mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]),
			   mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]));

	if (!dev)
		return MNL_CB_OK;

	port = calloc(1, sizeof(*port));
	if (!port)
		return MNL_CB_ERROR;

	port->dev = dev;
	port->index = mnl_attr_get_u32(tb[DEVLINK_ATTR_PORT_INDEX]);
	port->flavor = mnl_attr_get_u16(tb[DEVLINK_ATTR_PORT_FLAVOUR]);

	if (tb[DEVLINK_ATTR_PORT_NETDEV_NAME])
		port->netdev = strdup(mnl_attr_get_str(tb[DEVLINK_ATTR_PORT_NETDEV_NAME]));
	else {
		switch (port->flavor) {
		case DEVLINK_PORT_FLAVOUR_CPU:
			port->netdev = strdup("(cpu)");
			break;
		case DEVLINK_PORT_FLAVOUR_DSA:
			port->netdev = strdup("(dsa)");
			break;
		default:
			port->netdev = strdup("-");
			break;
		}
	}

	TAILQ_INSERT_TAIL(&dev->ports, port, node);
	return MNL_CB_OK;
}

static int env_dev_add(struct env *env, const char *asicid,
		       const char *busid, const char *devid)
{
	const struct chip *chip;
	struct dev *dev;

	for (chip = chips; chip->id; chip++) {
		if (!strcmp(chip->id, asicid))
			break;
	}

	if (!chip->id)
		return ENOSYS;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return ENOMEM;

	dev->env = env;
	dev->devlink.bus = strdup(busid);
	dev->devlink.dev = strdup(devid);
	dev->chip = chip;
	TAILQ_INIT(&dev->ports);

	if (TAILQ_FIRST(&env->devs))
		env->multichip = true;

	TAILQ_INSERT_TAIL(&env->devs, dev, node);
	return 0;
}

static int env_init_dev_cb(const struct nlmsghdr *nlh, void *_env)
{
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	struct env *env = _env;
	const char *id = NULL;
	struct nlattr *ver;
	int err;

	devlink_parse(nlh, tb);
	if (!tb[DEVLINK_ATTR_BUS_NAME] ||
	    !tb[DEVLINK_ATTR_DEV_NAME] ||
	    !tb[DEVLINK_ATTR_INFO_DRIVER_NAME])
		return MNL_CB_ERROR;

	if (strcmp("mv88e6xxx",
		   mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_DRIVER_NAME])))
		return MNL_CB_OK;

	mnl_attr_for_each(ver, nlh, sizeof(struct genlmsghdr)) {
		if (mnl_attr_get_type(ver) != DEVLINK_ATTR_INFO_VERSION_FIXED)
			continue;

		err = mnl_attr_parse_nested(ver, devlink_attr_cb, tb);
		if (err != MNL_CB_OK)
			continue;

		if (!tb[DEVLINK_ATTR_INFO_VERSION_NAME] ||
		    !tb[DEVLINK_ATTR_INFO_VERSION_VALUE])
			continue;

		if (strcmp("asic.id", mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_VERSION_NAME])))
			continue;

		id = mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_VERSION_VALUE]);
		break;
	}

	if (!id)
		return MNL_CB_OK;

	err = env_dev_add(env, id,
			  mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]),
			  mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]));
	if (err)
		return MNL_CB_ERROR;

	return MNL_CB_OK;
}

void env_init_dev_sort(struct env *env)
{
	struct dev *dev, *next, *insert;

	/* Sort devices on DSA index. Yes, use bubblesort. This list
	 * can contain a maximum of 32 devs, typically 1-3. */
	for (dev = TAILQ_FIRST(&env->devs); dev; dev = next) {
		next = TAILQ_NEXT(dev, node);
		if (!next)
			break;

		if (next->index > dev->index)
			continue;

		for (insert = next; insert && (insert->index < dev->index);
		     insert = TAILQ_NEXT(insert, node));

		TAILQ_REMOVE(&env->devs, dev, node);

		if (insert)
			TAILQ_INSERT_BEFORE(insert, dev, node);
		else
			TAILQ_INSERT_TAIL(&env->devs, dev, node);
	}

}

int env_init(struct env *env)
{
	struct nlmsghdr *nlh;
	struct dev *dev;
	int err;

	TAILQ_INIT(&env->devs);

	err = devlink_open(&env->dl);
	if (err)
		return err;

	nlh = devlink_msg_prepare(&env->dl, DEVLINK_CMD_INFO_GET,
				  NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP);

	err = devlink_query(&env->dl, nlh, env_init_dev_cb, env);
	if (err)
		return err;

	nlh = devlink_msg_prepare(&env->dl, DEVLINK_CMD_PORT_GET,
				  NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP);

	err = devlink_query(&env->dl, nlh, env_init_port_cb, env);
	if (err)
		return err;

	if (TAILQ_EMPTY(&env->devs))
		return ENODEV;

	TAILQ_FOREACH(dev, &env->devs, node) {
		err = dev_init(dev);
		if (err)
			return err;
	}

	env_init_dev_sort(env);
	return err;
}

void env_show_ports(struct env *env)
{
	struct port *port;
	struct dev *dev;
	uint16_t ctrl;

	puts("\e[7mNETDEV    P  LINK  MO  FL  S  L  PVID   FID\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (env->multichip)
			printf("\e[2m\e[7mDEV:%x %-37s\e[0m\n",
			       dev->index, dev->chip->id);

		TAILQ_FOREACH(port, &dev->ports, node) {
			port_init(port);
			ctrl = reg16(port->regs, 4);

			printf("%-8s  %x  %4s  %c%c  %c%c  %c %2s  %4u  %4u\n", port->netdev,
			       port->index,
			       port_link_str(port),
			       port_fmode_c[bits(ctrl,  8, 2)],
			       port_emode_c[bits(ctrl, 12, 2)],
			       bit(ctrl, 2) ? 'u' : '-',
			       bit(ctrl, 3) ? 'm' : '-',
			       port_state_c[bits(ctrl, 0, 2)],
			       port_lag_str(port),
			       bits(reg16(port->regs, 7), 0, 12),
			       port_op(port, fid)
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
	int err, i;

	puts("\e[7mADDRESS             FID  STATE      Q  F  0  1  2  3  4  5  6  7  8  9  a\e[0m");

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (env->multichip)
			printf("\e[2m\e[7mDEV:%x %-67s\e[0m\n",
			       dev->index, dev->chip->id);

		err = devlink_region_get(&env->dl, &dev->devlink, "atu",
					 devlink_region_dup_cb, &atu);
		if (err)
			break;

		kentry = (void *)atu.data.u8;
		while (!dev_op(dev, atu_parse, kentry, &entry)) {
			printf("%02x:%02x:%02x:%02x:%02x:%02x  %4u  %-9s  %c  %c",
			       entry.addr[0], entry.addr[1], entry.addr[2],
			       entry.addr[3], entry.addr[4], entry.addr[5],
			       entry.fid, (entry.addr[0] & 1) ?
			       atu_mc_str[entry.state.mc] : atu_uc_str[entry.state.uc],
			       prio_c(entry.qpri), prio_c(entry.fpri));

			for (i = 0; i < 11; i++) {
				if (bit(entry.portvec, i))
					printf("  %x", i);
				else
					fputs("  .", stdout);
			}

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
		if (err)
			break;

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

int main(int argc, char **argv)
{
	struct env env;

	if (env_init(&env)) {
		fprintf(stderr, "ERROR: Failed to discover devices.\n");
		return 1;
	}

	if (argc == 1) {
		env_show_vtu(&env); puts("");
		env_show_atu(&env); puts("");
		env_show_ports(&env);
	}

	if (!strcmp(argv[1], "port"))
		env_show_ports(&env);
	if (!strcmp(argv[1], "atu"))
		env_show_atu(&env);
	if (!strcmp(argv[1], "vtu"))
		env_show_vtu(&env);

	return 0;
}
