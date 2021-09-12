#ifndef __MDIO_NETLINK_H__
#define __MDIO_NETLINK_H__

#include <linux/types.h>

enum {
	MDIO_GENL_UNSPEC,
	MDIO_GENL_XFER,

	__MDIO_GENL_MAX,
	MDIO_GENL_MAX = __MDIO_GENL_MAX - 1
};

enum {
	MDIO_NLA_UNSPEC,
	MDIO_NLA_BUS_ID,  /* string */
	MDIO_NLA_TIMEOUT, /* u32 */
	MDIO_NLA_PROG,    /* struct mdio_nl_insn[] */
	MDIO_NLA_DATA,    /* nest */
	MDIO_NLA_ERROR,   /* s32 */

	__MDIO_NLA_MAX,
	MDIO_NLA_MAX = __MDIO_NLA_MAX - 1
};

enum mdio_nl_op {
	MDIO_NL_OP_UNSPEC,
	MDIO_NL_OP_READ,	/* read  dev(RI), port(RI), dst(R) */
	MDIO_NL_OP_WRITE,	/* write dev(RI), port(RI), src(RI) */
	MDIO_NL_OP_AND,		/* and   a(RI),   b(RI),    dst(R) */
	MDIO_NL_OP_OR,		/* or    a(RI),   b(RI),    dst(R) */
	MDIO_NL_OP_ADD,		/* add   a(RI),   b(RI),    dst(R) */
	MDIO_NL_OP_JEQ,		/* jeq   a(RI),   b(RI),    jmp(I) */
	MDIO_NL_OP_JNE,		/* jeq   a(RI),   b(RI),    jmp(I) */
	MDIO_NL_OP_EMIT,	/* emit  src(RI) */

	__MDIO_NL_OP_MAX,
	MDIO_NL_OP_MAX = __MDIO_NL_OP_MAX - 1
};

enum mdio_nl_argmode {
	MDIO_NL_ARG_NONE,
	MDIO_NL_ARG_REG,
	MDIO_NL_ARG_IMM,
	MDIO_NL_ARG_RESERVED
};

struct mdio_nl_insn {
	__u64 op:8;
	__u64 reserved:2;
	__u64 arg0:18;
	__u64 arg1:18;
	__u64 arg2:18;
};

#endif /* __MDIO_NETLINK_H__ */
