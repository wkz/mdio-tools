// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mdio-netlink.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/phy.h>
#include <net/genetlink.h>
#include <net/netlink.h>

static void c45_compat_convert(int *kdev, int *kreg, int udev, int ureg)
{
	if (!mdio_phy_id_is_c45(udev)) {
		*kdev = udev;
		*kreg = ureg;
	} else {
		*kdev = mdio_phy_id_prtad(udev);
		*kreg = MII_ADDR_C45 | (mdio_phy_id_devad(udev) << 16) | ureg;
	}
}

struct mdio_nl_xfer {
	struct genl_info *info;
	struct sk_buff *msg;
	void *hdr;
	struct nlattr *data;

	struct mii_bus *mdio;
	int timeout_ms;

	int prog_len;
	struct mdio_nl_insn *prog;
};

static int mdio_nl_open(struct mdio_nl_xfer *xfer);
static int mdio_nl_close(struct mdio_nl_xfer *xfer, bool last, int xerr);

static int mdio_nl_flush(struct mdio_nl_xfer *xfer)
{
	int err;

	err = mdio_nl_close(xfer, false, 0);
	if (err)
		return err;

	return mdio_nl_open(xfer);
}

static int mdio_nl_emit(struct mdio_nl_xfer *xfer, u32 datum)
{
	int err = 0;

	if (!nla_put_nohdr(xfer->msg, sizeof(datum), &datum))
		return 0;

	err = mdio_nl_flush(xfer);
	if (err)
		return err;

	return nla_put_nohdr(xfer->msg, sizeof(datum), &datum);
}

static inline u16 *__arg_r(u32 arg, u16 *regs)
{
	BUG_ON(arg >> 16 != MDIO_NL_ARG_REG);

	return &regs[arg & 0x7];
}

static inline u16 __arg_i(u32 arg)
{
	BUG_ON(arg >> 16 != MDIO_NL_ARG_IMM);

	return arg & 0xffff;
}

static inline u16 __arg_ri(u32 arg, u16 *regs)
{
	switch ((enum mdio_nl_argmode)(arg >> 16)) {
	case MDIO_NL_ARG_IMM:
		return arg & 0xffff;
	case MDIO_NL_ARG_REG:
		return regs[arg & 7];
	default:
		BUG();
	}
}

static int mdio_nl_eval(struct mdio_nl_xfer *xfer)
{
	struct mdio_nl_insn *insn;
	unsigned long timeout;
	u16 regs[8] = { 0 };
	unsigned int pc;
	int dev, reg;
	int ret = 0;

	timeout = jiffies + msecs_to_jiffies(xfer->timeout_ms);

	mutex_lock(&xfer->mdio->mdio_lock);

	for (insn = xfer->prog, pc = 0;
	     pc < xfer->prog_len;
	     insn = &xfer->prog[++pc]) {
		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			break;
		}

		switch ((enum mdio_nl_op)insn->op) {
		case MDIO_NL_OP_READ:
			c45_compat_convert(&dev, &reg,
					   __arg_ri(insn->arg0, regs),
					   __arg_ri(insn->arg1, regs));

			ret = __mdiobus_read(xfer->mdio, dev, reg);
			if (ret < 0)
				goto exit;
			*__arg_r(insn->arg2, regs) = ret;
			ret = 0;
			break;

		case MDIO_NL_OP_WRITE:
			c45_compat_convert(&dev, &reg,
					   __arg_ri(insn->arg0, regs),
					   __arg_ri(insn->arg1, regs));

			ret = __mdiobus_write(xfer->mdio, dev, reg,
					      __arg_ri(insn->arg2, regs));
			if (ret < 0)
				goto exit;
			ret = 0;
			break;

		case MDIO_NL_OP_AND:
			*__arg_r(insn->arg2, regs) =
				__arg_ri(insn->arg0, regs) &
				__arg_ri(insn->arg1, regs);
			break;

		case MDIO_NL_OP_OR:
			*__arg_r(insn->arg2, regs) =
				__arg_ri(insn->arg0, regs) |
				__arg_ri(insn->arg1, regs);
			break;

		case MDIO_NL_OP_ADD:
			*__arg_r(insn->arg2, regs) =
				__arg_ri(insn->arg0, regs) +
				__arg_ri(insn->arg1, regs);
			break;

		case MDIO_NL_OP_JEQ:
			if (__arg_ri(insn->arg0, regs) ==
			    __arg_ri(insn->arg1, regs))
				pc += (s16)__arg_i(insn->arg2);
			break;

		case MDIO_NL_OP_JNE:
			if (__arg_ri(insn->arg0, regs) !=
			    __arg_ri(insn->arg1, regs))
				pc += (s16)__arg_i(insn->arg2);
			break;

		case MDIO_NL_OP_EMIT:
			ret = mdio_nl_emit(xfer, __arg_ri(insn->arg0, regs));
			if (ret < 0)
				goto exit;
			ret = 0;
			break;

		case MDIO_NL_OP_UNSPEC:
		default:
			ret = -EINVAL;
			goto exit;
		}
	}
exit:
	mutex_unlock(&xfer->mdio->mdio_lock);
	return ret;
}

struct mdio_nl_op_proto {
	u8 arg0;
	u8 arg1;
	u8 arg2;
};

static const struct mdio_nl_op_proto mdio_nl_op_protos[MDIO_NL_OP_MAX + 1] = {
	[MDIO_NL_OP_READ] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_REG),
	},
	[MDIO_NL_OP_WRITE] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
	},
	[MDIO_NL_OP_AND] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_REG),
	},
	[MDIO_NL_OP_OR] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_REG),
	},
	[MDIO_NL_OP_ADD] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_REG),
	},
	[MDIO_NL_OP_JEQ] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_IMM),
	},
	[MDIO_NL_OP_JNE] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg2 = BIT(MDIO_NL_ARG_IMM),
	},
	[MDIO_NL_OP_EMIT] = {
		.arg0 = BIT(MDIO_NL_ARG_REG) | BIT(MDIO_NL_ARG_IMM),
		.arg1 = BIT(MDIO_NL_ARG_NONE),
		.arg2 = BIT(MDIO_NL_ARG_NONE),
	},
};

static int mdio_nl_validate_insn(const struct nlattr *attr,
				 struct netlink_ext_ack *extack,
				 const struct mdio_nl_insn *insn)
{
	const struct mdio_nl_op_proto *proto;

	if (insn->op > MDIO_NL_OP_MAX) {
		NL_SET_ERR_MSG_ATTR(extack, attr, "Illegal instruction");
		return -EINVAL;
	}

	proto = &mdio_nl_op_protos[insn->op];

	if (!(BIT(insn->arg0 >> 16) & proto->arg0)) {
		NL_SET_ERR_MSG_ATTR(extack, attr, "Argument 0 invalid");
		return -EINVAL;
	}

	if (!(BIT(insn->arg1 >> 16) & proto->arg1)) {
		NL_SET_ERR_MSG_ATTR(extack, attr, "Argument 1 invalid");
		return -EINVAL;
	}

	if (!(BIT(insn->arg2 >> 16) & proto->arg2)) {
		NL_SET_ERR_MSG_ATTR(extack, attr, "Argument 2 invalid");
		return -EINVAL;
	}

	return 0;
}

static int mdio_nl_validate_prog(const struct nlattr *attr,
				 struct netlink_ext_ack *extack)
{
	const struct mdio_nl_insn *prog = nla_data(attr);
	int len = nla_len(attr);
	int i, err = 0;

	if (len % sizeof(*prog)) {
		NL_SET_ERR_MSG_ATTR(extack, attr, "Unaligned instruction");
		return -EINVAL;
	}

	len /= sizeof(*prog);
	for (i = 0; i < len; i++) {
		err = mdio_nl_validate_insn(attr, extack, &prog[i]);
		if (err) {
			break;
		}
	}

	return err;
}

static const struct nla_policy mdio_nl_policy[MDIO_NLA_MAX + 1] = {
	[MDIO_NLA_UNSPEC]  = { .type = NLA_UNSPEC, },
	[MDIO_NLA_BUS_ID]  = { .type = NLA_STRING, .len = MII_BUS_ID_SIZE },
	[MDIO_NLA_TIMEOUT] = NLA_POLICY_MAX(NLA_U16, 10 * MSEC_PER_SEC),
	[MDIO_NLA_PROG]    = NLA_POLICY_VALIDATE_FN(NLA_BINARY,
						    mdio_nl_validate_prog,
						    0x1000),
	[MDIO_NLA_DATA]    = { .type = NLA_NESTED },
	[MDIO_NLA_ERROR]   = { .type = NLA_S32, },
};

static struct genl_family mdio_nl_family;

static int mdio_nl_open(struct mdio_nl_xfer *xfer)
{
	int err;

	xfer->msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!xfer->msg) {
		err = -ENOMEM;
		goto err;
	}

	xfer->hdr = genlmsg_put(xfer->msg, xfer->info->snd_portid,
				xfer->info->snd_seq, &mdio_nl_family,
				NLM_F_ACK | NLM_F_MULTI, MDIO_GENL_XFER);
	if (!xfer->hdr) {
		err = -EMSGSIZE;
		goto err_free;
	}

	xfer->data = nla_nest_start(xfer->msg, MDIO_NLA_DATA);
	if (!xfer->data) {
		err = -EMSGSIZE;
		goto err_free;
	}

	return 0;

err_free:
	nlmsg_free(xfer->msg);
err:
	return err;
}

static int mdio_nl_close(struct mdio_nl_xfer *xfer, bool last, int xerr)
{
	struct nlmsghdr *end;
	int err;

	nla_nest_end(xfer->msg, xfer->data);

	if (xerr && nla_put_s32(xfer->msg, MDIO_NLA_ERROR, xerr)) {
		err = mdio_nl_flush(xfer);
		if (err)
			goto err_free;

		if (nla_put_s32(xfer->msg, MDIO_NLA_ERROR, xerr)) {
			err = -EMSGSIZE;
			goto err_free;
		}
	}

	genlmsg_end(xfer->msg, xfer->hdr);

	if (last) {
		end = nlmsg_put(xfer->msg, xfer->info->snd_portid,
				xfer->info->snd_seq, NLMSG_DONE, 0,
				NLM_F_ACK | NLM_F_MULTI);
		if (!end) {
			err = mdio_nl_flush(xfer);
			if (err)
				goto err_free;

			end = nlmsg_put(xfer->msg, xfer->info->snd_portid,
					xfer->info->snd_seq, NLMSG_DONE, 0,
					NLM_F_ACK | NLM_F_MULTI);
			if (!end) {
				err = -EMSGSIZE;
				goto err_free;
			}
		}
	}

	return genlmsg_unicast(genl_info_net(xfer->info), xfer->msg,
			       xfer->info->snd_portid);

err_free:
	nlmsg_free(xfer->msg);
	return err;
}

static int mdio_nl_cmd_xfer(struct sk_buff *skb, struct genl_info *info)
{
	struct mdio_nl_xfer xfer;
	int err;

	if (!info->attrs[MDIO_NLA_BUS_ID] ||
	    !info->attrs[MDIO_NLA_PROG]   ||
	     info->attrs[MDIO_NLA_DATA]   ||
	     info->attrs[MDIO_NLA_ERROR])
		return -EINVAL;

	xfer.mdio = mdio_find_bus(nla_data(info->attrs[MDIO_NLA_BUS_ID]));
	if (!xfer.mdio)
		return -ENODEV;

	if (info->attrs[MDIO_NLA_TIMEOUT])
		xfer.timeout_ms = nla_get_u32(info->attrs[MDIO_NLA_TIMEOUT]);
	else
		xfer.timeout_ms = 100;

	xfer.info = info;
	xfer.prog_len = nla_len(info->attrs[MDIO_NLA_PROG]) / sizeof(*xfer.prog);
	xfer.prog = nla_data(info->attrs[MDIO_NLA_PROG]);

	err = mdio_nl_open(&xfer);
	if (err)
		goto out_put;

	err = mdio_nl_eval(&xfer);

	err = mdio_nl_close(&xfer, true, err);

out_put:
	put_device(&xfer.mdio->dev);
	return err;
}

static const struct genl_ops mdio_nl_ops[] = {
	{
		.cmd = MDIO_GENL_XFER,
		.doit = mdio_nl_cmd_xfer,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family mdio_nl_family = {
	.name     = "mdio",
	.version  = 1,
	.maxattr  = MDIO_NLA_MAX,
	.netnsok  = false,
	.module   = THIS_MODULE,
	.ops      = mdio_nl_ops,
	.n_ops    = ARRAY_SIZE(mdio_nl_ops),
	.policy   = mdio_nl_policy,
};

static int __init mdio_nl_init(void)
{
	return genl_register_family(&mdio_nl_family);
}

static void __exit mdio_nl_exit(void)
{
	genl_unregister_family(&mdio_nl_family);
}

MODULE_AUTHOR("Tobias Waldekranz <tobias@waldekranz.com>");
MODULE_DESCRIPTION("MDIO Netlink Interface");
MODULE_LICENSE("GPL");

module_init(mdio_nl_init);
module_exit(mdio_nl_exit);
