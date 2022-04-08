#ifndef __MVLS_H
#define __MVLS_H

#include <errno.h>
#include <sys/queue.h>

#include "devlink.h"

#define offsetof(type, member)  __builtin_offsetof (type, member)
#define container_of(ptr, type, member) ({			     \
                const typeof( ((type *)0)->member ) *__mptr = (ptr); \
                (type *)( (char *)__mptr - offsetof(type,member) );})

#define reg16(_region, _reg) ((_region).data.u16[_reg])
#define bits(_reg, _bit, _n) (((_reg) >> (_bit)) & ((1 << (_n)) - 1))
#define bit(_reg, _bit) bits(_reg, _bit, 1)

#define MAX_PORTS 11

enum vtu_member {
	VTU_UNMODIFIED = 0,
	VTU_UNTAGGED   = 1,
	VTU_TAGGED     = 2,
	VTU_NOT_MEMBER = 3
};

struct prio_override {
	uint8_t set:1;
	uint8_t pri:3;
};

struct vtu_entry {
	uint16_t vid;
	uint16_t fid;
	uint8_t  sid;

	enum vtu_member member[MAX_PORTS];

	bool policy;

	struct prio_override qpri;
	struct prio_override fpri;
};

enum stu_state {
	STU_DISABLED   = 0,
	STU_BLOCKING   = 1,
	STU_LEARNING   = 2,
	STU_FORWARDING = 3
};

struct stu_entry {
	uint8_t  sid;

	enum stu_state state[MAX_PORTS];
};

enum atu_uc_state {
	ATU_UC_UNUSED    = 0x0,
	ATU_UC_D_AGE_1   = 0x1,
	ATU_UC_D_AGE_2   = 0x2,
	ATU_UC_D_AGE_3   = 0x3,
	ATU_UC_D_AGE_4   = 0x4,
	ATU_UC_D_AGE_5   = 0x5,
	ATU_UC_D_AGE_6   = 0x6,
	ATU_UC_D_AGE_7   = 0x7,
	ATU_UC_S_PCY     = 0x8,
	ATU_UC_S_PCY_PO  = 0x9,
	ATU_UC_S_NRL     = 0xa,
	ATU_UC_S_NRL_PO  = 0xb,
	ATU_UC_S_MGMT    = 0xc,
	ATU_UC_S_MGMT_PO = 0xd,
	ATU_UC_S         = 0xe,
	ATU_UC_S_PO      = 0xf
};

enum atu_mc_state {
	ATU_MC_UNUSED  = 0x0,
	ATU_MC_RESVD_1 = 0x1,
	ATU_MC_RESVD_2 = 0x2,
	ATU_MC_RESVD_3 = 0x3,
	ATU_MC_PCY     = 0x4,
	ATU_MC_NRL     = 0x5,
	ATU_MC_MGMT    = 0x6,
	ATU_MC         = 0x7,
	ATU_MC_RESVD_8 = 0x8,
	ATU_MC_RESVD_9 = 0x9,
	ATU_MC_RESVD_A = 0xa,
	ATU_MC_RESVD_B = 0xb,
	ATU_MC_PCY_PO  = 0xc,
	ATU_MC_NRL_PO  = 0xd,
	ATU_MC_MGMT_PO = 0xe,
	ATU_MC_PO      = 0xf
};

struct atu_entry {
	uint16_t fid;
	uint8_t addr[6];

	struct prio_override qpri;
	struct prio_override fpri;

	bool lag;
	uint16_t portvec;

	union {
		enum atu_uc_state uc;
		enum atu_mc_state mc;
	} state;
};

struct port;
struct dev;

struct family {
	int (*port_lag)(struct port *port);
	uint16_t (*port_fid)(struct port *port);

	int (*dev_atu_parse)(struct dev *dev,
			     struct mv88e6xxx_devlink_atu_entry *kentry,
			     struct atu_entry *entry);
	int (*dev_vtu_parse)(struct dev *dev,
			     struct mv88e6xxx_devlink_vtu_entry *kentry,
			     struct vtu_entry *entry);
	int (*dev_stu_parse)(struct dev *dev,
			     struct mv88e6xxx_devlink_stu_entry *kentry,
			     struct stu_entry *entry);
};

struct chip {
	char *id;
	const struct family *family;

	int n_ports;
};

struct port {
	struct dev *dev;
	int index;
	char *netdev;
	enum devlink_port_flavour flavor;

	struct devlink_region regs;

	TAILQ_ENTRY(port) node;
};
TAILQ_HEAD(port_list, port);

int port_load_regs(struct port *port);

#define port_op(_port, _op, ...) \
	((_port)->dev->chip->family-> port_ ## _op )((_port), ##__VA_ARGS__)

struct dev {
	struct env *env;
	struct devlink_addr devlink;
	const struct chip *chip;

	int index;
	struct port_list ports;

	struct devlink_region global1;
	struct devlink_region pvt;

	TAILQ_ENTRY(dev) node;
};
TAILQ_HEAD(dev_list, dev);

int dev_load_pvt(struct dev *dev);

#define dev_op(_dev, _op, ...) \
	((_dev)->chip->family-> dev_ ## _op )((_dev), ##__VA_ARGS__)

struct env {
	bool multichip:1;

	struct devlink dl;
	struct dev_list devs;
};

#define env_from_dl(_dl) container_of(_dl, struct env, dl)

struct printer {
	void (*env_print_ports)(struct env *env);
	void (*env_print_atu)(struct env *env);
	void (*env_print_vtu)(struct env *env);
	void (*env_print_stu)(struct env *env);

	void (*env_print_pvt)(struct env *env);
	void (*dev_print_pvt)(struct dev *dev);

	void (*prologue)(void);
	void (*join)(void);
	void (*epilogue)(void);
};

static inline int env_print_ports(const struct printer *p, struct env *env)
{
	if (!p->env_print_ports)
		return -ENOENT;

	p->env_print_ports(env);
	return 0;
}

static inline int env_print_atu(const struct printer *p, struct env *env)
{
	if (!p->env_print_atu)
		return -ENOENT;

	p->env_print_atu(env);
	return 0;
}

static inline int env_print_vtu(const struct printer *p, struct env *env)
{
	if (!p->env_print_vtu)
		return -ENOENT;

	p->env_print_vtu(env);
	return 0;
}

static inline int env_print_stu(const struct printer *p, struct env *env)
{
	if (!p->env_print_stu)
		return -ENOENT;

	p->env_print_stu(env);
	return 0;
}

static inline int env_print_pvt(const struct printer *p, struct env *env)
{
	if (!p->env_print_pvt)
		return -ENOENT;

	p->env_print_pvt(env);
	return 0;
}

static inline int dev_print_pvt(const struct printer *p, struct dev *dev)
{
	if (!p->dev_print_pvt)
		return -ENOENT;

	p->dev_print_pvt(dev);
	return 0;
}

static inline int print_prologue(const struct printer *p)
{
	if (!p->prologue)
		return -ENOENT;

	p->prologue();
	return 0;
}

static inline int print_join(const struct printer *p)
{
	if (!p->join)
		return -ENOENT;

	p->join();
	return 0;
}

static inline int print_epilogue(const struct printer *p)
{
	if (!p->epilogue)
		return -ENOENT;

	p->epilogue();
	return 0;
}

extern const struct printer printer_show;

#endif	/* __MVLS_H */
