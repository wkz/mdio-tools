// SPDX-License-Identifier: GPL-2.0

#ifndef _COMPAT_H_
#define _COMPAT_H_

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
#include <linux/device.h>
#include <linux/phy.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
#include <linux/string.h>

static inline int _mdio_find_bus_match(struct device *dev, void *data)
{
	return dev->parent && sysfs_streq(dev_name(dev->parent), data);
}
#else
static inline int _mdio_find_bus_match(struct device *dev, const void *data)
{
	return dev->parent && device_match_name(dev->parent, data);
}
#endif

/**
 * mdio_find_bus - Given the name of a mdiobus, find the mii_bus.
 * @mdio_bus_np: Pointer to the mii_bus.
 *
 * Returns a reference to the mii_bus, or NULL if none found.  The
 * embedded struct device will have its reference count incremented,
 * and this must be put_deviced'ed once the bus is finished with.
 *
 * Abuse bus_find_device() to * reimplement the mdio_find_bus()
 * function introduced in v5.7.
 */
static inline struct mii_bus *mdio_find_bus(const char *mdio_name)
{
	struct device *d, *p = NULL;

	d = bus_find_device(&mdio_bus_type, NULL, (char *)mdio_name, _mdio_find_bus_match);
	if (!d)
		return NULL;
	if (d->parent)
		p = get_device(d->parent);
	put_device(d);
	return p ? to_mii_bus(p) : NULL;
}
#endif

#endif /* _COMPAT_H_ */
