#include "config.h"

#include <err.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "mvls.h"

static const char *port_state[] = { "disabled", "blocking", "learning", "forwarding" };
static const char *port_fmode[] = { "normal", "dsa", "provider", "edsa" };
static const char *port_emode[] = { "unmodified", "untagged", "tagged", "edsa" };
static const char *port_8021q[] = { "disabled", "fallback", "check", "secure" };

static const char *atu_uc_str[] = {
	"unused", "age-1",     "age-2",  "age-3",
	"age-4",  "age-5"  ,   "age-6",  "age-7",
	"policy", "policy-po", "nrl",    "nrl-po",
	"mgmt",   "mgmt-po",   "static", "static-po"
};

static const char *atu_mc_str[] = {
	"unused",   "resvd1",  "resvd2",  "resvd3",
	"policy",   "nrl",     "mgmt",    "static",
	"resvd8",   "resvd9",  "resvda",  "resvdb",
	"policy-po", "nrl-po", "mgmt-po", "static-po"
};

static void env_json_ports(struct env *env)
{
	uint16_t ctrl, ctrl2;
	struct port *port;
	bool first = true;
	struct dev *dev;
	int lag;

	fputs("\"ports\":[", stdout);

	TAILQ_FOREACH(dev, &env->devs, node) {
		TAILQ_FOREACH(port, &dev->ports, node) {
			if (first)
				first = false;
			else
				putchar(',');

			port_load_regs(port);
			ctrl = reg16(port->regs, 4);
			ctrl2 = reg16(port->regs, 8);

			putchar('{');

			printf("\"netdev\":\"%s\","
			       "\"dev\":%d,"
			       "\"port\":%d,"
			       "\"state\":\"%s\","
			       "\"frame-mode\":\"%s\","
			       "\"egress-mode\":\"%s\","
			       "\"802.1q\":\"%s\","
			       "\"pvid\":%u,"
			       "\"fid\":%u"
			       ,
			       port->netdev,
			       dev->index,
			       port->index,
			       port_state[bits(ctrl,  0, 2)],
			       port_fmode[bits(ctrl,  8, 2)],
			       port_emode[bits(ctrl, 12, 2)],
			       port_8021q[bits(ctrl2, 10, 2)],
			       bits(reg16(port->regs, 7), 0, 12),
			       port_op(port, fid));

			lag = port_op(port, lag);
			if (lag >= 0)
				printf(",\"lag\":%d", lag);

			if (bit(ctrl, 2))
				fputs(",\"flood-unicast\":true", stdout);
			if (bit(ctrl, 3))
				fputs(",\"flood-multicast\":true", stdout);

			if (bit(ctrl2, 8))
				fputs(",\"allow-untagged\":true", stdout);
			if (bit(ctrl2, 9))
				fputs(",\"allow-tagged\":true", stdout);

			putchar('}');
		}
	}

	fputs("]", stdout);
}

static void json_print_portvec(const char *name, uint16_t portvec)
{
	bool first = true;
	int i;

	printf(",\"%s\":[", name);

	for (i = 0; i < 11; i++) {
		if (!bit(portvec, i))
			continue;

		if (first)
			first = false;
		else
			putchar(',');

		printf("%d", i);
	}

	putchar(']');
}

static void env_json_atu(struct env *env)
{
	struct mv88e6xxx_devlink_atu_entry *kentry;
	struct devlink_region atu = { 0 };
	struct atu_entry entry;
	bool first = true;
	struct dev *dev;
	int err;

	fputs("\"atu\":[", stdout);

	TAILQ_FOREACH(dev, &env->devs, node) {
		err = devlink_region_get(&env->dl, &dev->devlink, "atu",
					 devlink_region_dup_cb, &atu);
		if (err) {
			warn("failed querying atu");
			break;
		}

		kentry = (void *)atu.data.u8;
		while (!dev_op(dev, atu_parse, kentry, &entry)) {
			if (first)
				first = false;
			else
				putchar(',');

			putchar('{');

			printf("\"dev\":%d,"
			       "\"fid\":%u,"
			       "\"address\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
			       "\"state\":\"%s\""
			       ,
			       dev->index,
			       entry.fid,
			       entry.addr[0], entry.addr[1], entry.addr[2],
			       entry.addr[3], entry.addr[4], entry.addr[5],
			       (entry.addr[0] & 1) ?
			       atu_mc_str[entry.state.mc] : atu_uc_str[entry.state.uc]);

			if (entry.qpri.set)
				printf(",\"queue-prio-override\": %u", entry.qpri.pri);

			if (entry.fpri.set)
				printf(",\"frame-prio-override\": %u", entry.fpri.pri);

			if (entry.lag) {
				printf(",\"lag\": %u", entry.portvec);
				goto next;
			}

			json_print_portvec("ports", entry.portvec);

		next:
			putchar('}');
			kentry++;
		}

		devlink_region_free(&atu);
	}

	fputs("]", stdout);
}

void env_json_vtu(struct env *env)
{
	struct mv88e6xxx_devlink_vtu_entry *kentry;
	uint16_t unmod = 0, untag = 0, tag = 0;
	struct devlink_region vtu = { 0 };
	struct vtu_entry entry;
	bool first = true;
	struct dev *dev;
	int err, i;

	fputs("\"vtu\":[", stdout);

	TAILQ_FOREACH(dev, &env->devs, node) {
		err = devlink_region_get(&env->dl, &dev->devlink, "vtu",
					 devlink_region_dup_cb, &vtu);
		if (err) {
			warn("failed querying vtu");
			break;
		}

		kentry = (void *)vtu.data.u8;
		while (!dev_op(dev, vtu_parse, kentry, &entry)) {
			if (first)
				first = false;
			else
				putchar(',');

			putchar('{');

			printf("\"dev\":%d,"
			       "\"vid\":%u,"
			       "\"fid\":%u,"
			       "\"sid\":%u"
			       ,
			       dev->index,
			       entry.vid, entry.fid, entry.sid);

			if (entry.policy)
				fputs(",\"policy\": true", stdout);

			if (entry.qpri.set)
				printf(",\"queue-prio-override\": %u", entry.qpri.pri);

			if (entry.fpri.set)
				printf(",\"frame-prio-override\": %u", entry.fpri.pri);

			for (i = 0; i < 11; i++) {
				switch (entry.member[i]) {
				case VTU_UNMODIFIED:
					unmod |= (1 << i);
					break;
				case VTU_UNTAGGED:
					untag |= (1 << i);
					break;
				case VTU_TAGGED:
					tag |= (1 << i);
					break;
				case VTU_NOT_MEMBER:
					break;
				}
			}

			json_print_portvec("unmodified", unmod);
			json_print_portvec("untagged", untag);
			json_print_portvec("tagged", tag);

			putchar('}');
			kentry++;
		}

		devlink_region_free(&vtu);
	}

	fputs("]", stdout);
}

void env_json_stu(struct env *env)
{
	uint16_t dis = 0, blk = 0, lrn = 0, fwd = 0;
	struct mv88e6xxx_devlink_stu_entry *kentry;
	struct devlink_region stu = { 0 };
	struct stu_entry entry;
	bool first = true;
	struct dev *dev;
	int err, i;

	fputs("\"stu\":[", stdout);

	TAILQ_FOREACH(dev, &env->devs, node) {
		err = devlink_region_get(&env->dl, &dev->devlink, "stu",
					 devlink_region_dup_cb, &stu);
		if (err) {
			warn("failed querying stu");
			break;
		}

		kentry = (void *)stu.data.u8;
		while (!dev_op(dev, stu_parse, kentry, &entry)) {
			if (first)
				first = false;
			else
				putchar(',');

			putchar('{');

			printf("\"dev\":%d,"
			       "\"sid\":%u"
			       ,
			       dev->index,
			       entry.sid);

			for (i = 0; i < 11; i++) {
				switch (entry.state[i]) {
				case STU_DISABLED:
					dis |= (1 << i);
					break;
				case STU_BLOCKING:
					blk |= (1 << i);
					break;
				case STU_LEARNING:
					lrn |= (1 << i);
					break;
				case STU_FORWARDING:
					fwd |= (1 << i);
					break;
				}
			}

			json_print_portvec("disabled", dis);
			json_print_portvec("blocking", blk);
			json_print_portvec("learning", lrn);
			json_print_portvec("forwarding", fwd);

			putchar('}');
			kentry++;
		}

		devlink_region_free(&stu);
	}

	fputs("]", stdout);
}

void json_prologue(void)
{
	putchar('{');
}

void json_join(void)
{
	putchar(',');
}

void json_epilogue(void)
{
	putchar('}');
}

const struct printer printer_json = {
	.env_print_ports = env_json_ports,
	.env_print_vtu = env_json_vtu,
	.env_print_atu = env_json_atu,
	.env_print_stu = env_json_stu,
	/* .env_print_pvt = env_json_pvt, */
	/* .dev_print_pvt = dev_json_pvt, */

	.prologue = json_prologue,
	.join     = json_join,
	.epilogue = json_epilogue,
};
