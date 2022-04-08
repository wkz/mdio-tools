#ifndef __DEVLINK_H
#define __DEVLINK_H

#include <stdlib.h>

#include <libmnl/libmnl.h>
#include <linux/devlink.h>
#include <linux/genetlink.h>

struct mv88e6xxx_devlink_atu_entry {
	/* The FID is scattered over multiple registers. */
	uint16_t fid;
	uint16_t atu_op;
	uint16_t atu_data;
	uint16_t atu_01;
	uint16_t atu_23;
	uint16_t atu_45;
};

struct mv88e6xxx_devlink_vtu_entry {
	uint16_t fid;
	uint16_t sid;
	uint16_t op;
	uint16_t vid;
	uint16_t data[3];
	uint16_t resvd;
};

struct mv88e6xxx_devlink_stu_entry {
	uint16_t sid;
	uint16_t vid;
	uint16_t data[3];
	uint16_t resvd;
};

struct devlink_addr {
	char *bus;
	char *dev;
};

struct devlink_region {
	union {
		uint8_t *u8;
		uint16_t *u16;
	} data;

	size_t size;
};

static inline int devlink_region_loaded(struct devlink_region *region)
{
	return region->size != 0;
}

static inline void devlink_region_free(struct devlink_region *region)
{
	if (region->data.u8)
		free(region->data.u8);

	memset(region, 0, sizeof(*region));
}

struct devlink {
	struct mnl_socket *nl;
	uint8_t *buf;

	uint32_t family;
	uint8_t version;

	unsigned int portid;
	unsigned int seq;
};


struct nlmsghdr *devlink_msg_prepare(struct devlink *dl, uint8_t cmd,
				     uint16_t flags);

int devlink_attr_cb(const struct nlattr *attr, void *data);
int devlink_parse(const struct nlmsghdr *nlh, struct nlattr **tb);
int devlink_query(struct devlink *dl, struct nlmsghdr *nlh,
		  mnl_cb_t cb, void *data);

int devlink_region_dup_cb(const struct nlmsghdr *nlh, void *_region);
int devlink_port_region_get(struct devlink *dl, struct devlink_addr *addr,
			    int port, const char *name,
			    mnl_cb_t cb, void *data);
int devlink_region_get(struct devlink *dl, struct devlink_addr *addr,
		       const char *name, mnl_cb_t cb, void *data);

int devlink_open(struct devlink *dl);

#endif	/* __DEVLINK_H */
