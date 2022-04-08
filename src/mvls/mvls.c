#include "config.h"

#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "devlink.h"
#include "mvls.h"

static int __dev_vtu_parse(struct dev *dev,
			   struct mv88e6xxx_devlink_vtu_entry *kentry,
			   struct vtu_entry *entry, uint8_t mbits)
{
	int i;

	if (!bit(kentry->vid, 12)) {
		errno = ENODATA;
		return -1;
	}

	entry->vid = bits(kentry->vid, 0, 12);
	entry->fid = bits(kentry->fid, 0, 12);
	entry->sid = bits(kentry->sid, 0, 6);

	for (i = 0; i < 11; i++)
		entry->member[i] = bits(kentry->data[i / (16 / mbits)],
					(i * mbits) & 0xf, 2);

	entry->policy = bit(kentry->fid, 12);

	/* TODO */
	entry->qpri.set = 0;
	entry->fpri.set = 0;
	return 0;
}

static int peridot_dev_vtu_parse(struct dev *dev,
				 struct mv88e6xxx_devlink_vtu_entry *kentry,
				 struct vtu_entry *entry)
{
	return __dev_vtu_parse(dev, kentry, entry, 2);
}

static int opal_dev_vtu_parse(struct dev *dev,
			      struct mv88e6xxx_devlink_vtu_entry *kentry,
			      struct vtu_entry *entry)
{
	return __dev_vtu_parse(dev, kentry, entry, 4);
}

static int __dev_stu_parse(struct dev *dev,
			   struct mv88e6xxx_devlink_stu_entry *kentry,
			   struct stu_entry *entry, uint8_t mbits)
{
	uint8_t offs = (mbits == 4) ? 2 : 0;
	int i;

	if (!bit(kentry->vid, 12)) {
		errno = ENODATA;
		return -1;
	}

	entry->sid = bits(kentry->sid, 0, 6);

	for (i = 0; i < 11; i++)
		entry->state[i] = bits(kentry->data[i / (16 / mbits)],
				       (i * mbits + offs) & 0xf, 2);
	return 0;
}

static int peridot_dev_stu_parse(struct dev *dev,
				 struct mv88e6xxx_devlink_stu_entry *kentry,
				 struct stu_entry *entry)
{
	return __dev_stu_parse(dev, kentry, entry, 2);
}

static int opal_dev_stu_parse(struct dev *dev,
			      struct mv88e6xxx_devlink_stu_entry *kentry,
			      struct stu_entry *entry)
{
	return __dev_stu_parse(dev, kentry, entry, 4);
}

int opal_dev_atu_parse(struct dev *dev,
		       struct mv88e6xxx_devlink_atu_entry *kentry,
		       struct atu_entry *entry)
{
	if (!bits(kentry->atu_data, 0, 4)) {
		errno = ENODATA;
		return -1;
	}

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
	.dev_stu_parse = opal_dev_stu_parse,
};

const struct family peridot_family = {
	.port_lag = opal_port_lag,
	.port_fid = opal_port_fid,

	.dev_atu_parse = opal_dev_atu_parse,
	.dev_vtu_parse = peridot_dev_vtu_parse,
};

const struct family amethyst_family = {
	.port_lag = opal_port_lag,
	.port_fid = opal_port_fid,

	.dev_atu_parse = opal_dev_atu_parse,
	.dev_vtu_parse = peridot_dev_vtu_parse,
	.dev_stu_parse = peridot_dev_stu_parse,
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
		.id = "Marvell 88E6190",
		.family = &peridot_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6190X",
		.family = &peridot_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6191",
		.family = &peridot_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6191X",
		.family = &amethyst_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6193X",
		.family = &amethyst_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6290",
		.family = &peridot_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6390",
		.family = &peridot_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6390X",
		.family = &peridot_family,
		.n_ports = 11,
	},
	{
		.id = "Marvell 88E6393X",
		.family = &amethyst_family,
		.n_ports = 11,
	},

	{ .id = NULL }
};

int port_load_regs(struct port *port)
{
	struct dev *dev = port->dev;
	struct env *env = dev->env;

	if (devlink_region_loaded(&port->regs))
		return 0;

	return devlink_port_region_get(&env->dl, &dev->devlink, port->index,
				       "port", devlink_region_dup_cb, &port->regs);
}

int dev_load_pvt(struct dev *dev)
{
	if (devlink_region_loaded(&dev->pvt))
		return 0;

	return devlink_region_get(&dev->env->dl, &dev->devlink,
				  "pvt", devlink_region_dup_cb, &dev->pvt);
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


static struct dev *env_dev_get(struct env *env, int index)
{
	struct dev *dev;

	TAILQ_FOREACH(dev, &env->devs, node) {
		if (dev->index == index)
			return dev;
	}

	errno = EINVAL;
	return NULL;
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

	errno = ENODEV;
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

	if (TAILQ_EMPTY(&env->devs)) {
		errno = ENODEV;
		return -1;
	}

	TAILQ_FOREACH(dev, &env->devs, node) {
		err = dev_init(dev);
		if (err)
			return err;
	}

	env_init_dev_sort(env);
	return err;
}


int usage(int rc)
{
	fputs("Usage: mvls [OPT] [CMD]\n"
	      "\n"
	      "Options:\n"
	      "  -h   This help text\n"
	      "  -j   Use JSON for all output\n"
	      "  -v   Show verision and contact information\n"
	      "\n"
	      "Commands:\n"
	      "  port\n"
	      "    Displays and overview of switchcore ports and their properties.\n"
	      "\n"
	      "  atu\n"
	      "    Displays the contents of the ATU with VLAN and port vectors.\n"
	      "\n"
	      "  vtu\n"
	      "    Displays the contents of the VTU with FID and port mappings.\n"
	      "    VLAN membership states:\n"
	      "    .   Not a member\n"
	      "    u   Member, egress untagged\n"
	      "    t   Member, egress tagged\n"
	      "    =   Member, egress unmodified\n"
	      "\n"
	      "  stu\n"
	      "    Displays the contents of the STU.\n"
	      "    STU states:\n"
	      "    -   Disabled\n"
	      "    B   Blocking/listening\n"
	      "    L   Learning\n"
	      "    f   Forwarding\n"
	      "\n"
	      "  pvt [DEV]\n"
	      ""
	      "    Displays the contents of the Port VLAN Table. Without DEV, a\n"
	      "    condensed view of the isolation state between all ports is shown:\n"
	      "    .   Full isolation\n"
	      "    x   Bidirectional communication allowed\n"
	      "    <   Only communication from column to row port allowed\n"
	      "    ^   Only communication from row to column port allowed\n"
	      "\n"
	      "    If DEV is supplied, dump the full PVT for that device.\n"
	      "\n"
	      "By default, mvls displays an overview of the VTU, ATU and ports.\n"
	      , stdout);

	return rc;
}

int main(int argc, char **argv)
{
	const struct printer *p = &printer_show;
	struct env env;
	int c;

	while ((c = getopt(argc, argv, "hjv")) != EOF) {
		switch (c) {
		case 'h':
			return usage(0);

		case 'j':
			p = &printer_json;
			break;

		case 'v':
			puts("v" PACKAGE_VERSION);
			puts("\nBug report address: " PACKAGE_BUGREPORT);
			return 0;

		default:
			return usage(1);
		}
	}

	if (env_init(&env))
		err(1, "failed discovering any devices");

	print_prologue(p);

	if (optind == argc) {
		env_print_vtu(p, &env);
		print_join(p);
		env_print_stu(p, &env);
		print_join(p);
		env_print_atu(p, &env);
		print_join(p);
		env_print_ports(p, &env);
		goto epilogue;
	}

	if (!strcmp(argv[optind], "port"))
		env_print_ports(p, &env);
	if (!strcmp(argv[optind], "atu"))
		env_print_atu(p, &env);
	if (!strcmp(argv[optind], "vtu"))
		env_print_vtu(p, &env);
	if (!strcmp(argv[optind], "stu"))
		env_print_stu(p, &env);
	if (!strcmp(argv[optind], "pvt")) {
		struct dev *dev;
		int index;

		if (++optind == argc) {
			env_print_pvt(p, &env);
			goto epilogue;
		}

		index = strtol(argv[optind], NULL, 0);
		dev = env_dev_get(&env, index);
		if (!dev)
			err(1, "unknown device index \"%s\"", argv[optind]);

		dev_print_pvt(p, dev);
	}

epilogue:
	print_epilogue(p);
	return 0;
}
