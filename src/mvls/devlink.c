#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "devlink.h"

int devlink_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type;

	/* Strategy: Hope for the best (tm) */
	type = mnl_attr_get_type(attr);
	tb[type] = attr;
	return MNL_CB_OK;
}

int devlink_parse(const struct nlmsghdr *nlh, struct nlattr **tb)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);

	mnl_attr_parse(nlh, sizeof(*genl), devlink_attr_cb, tb);
	return 0;
}

static struct nlmsghdr *__msg_prepare(struct devlink *dl, uint8_t cmd,
				      uint16_t flags, uint32_t family,
				      uint8_t version)
{
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

	nlh = mnl_nlmsg_put_header(dl->buf);
	nlh->nlmsg_type	= family;
	nlh->nlmsg_flags = flags;
	dl->seq = time(NULL);
	nlh->nlmsg_seq = dl->seq;

	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
	genl->cmd = cmd;
	genl->version = version;
	return nlh;
}

static int __cb_noop(const struct nlmsghdr *nlh, void *data)
{
	return MNL_CB_OK;
}

static int __cb_error(const struct nlmsghdr *nlh, void *data)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);

	/* Netlink subsystems returns the errno value with different signess */
	if (err->error < 0)
		errno = -err->error;
	else
		errno = err->error;

	return err->error == 0 ? MNL_CB_STOP : MNL_CB_ERROR;
}

static int __cb_stop(const struct nlmsghdr *nlh, void *data)
{
	int len = *(int *)NLMSG_DATA(nlh);

	if (len < 0) {
		errno = -len;
		return MNL_CB_ERROR;
	}
	return MNL_CB_STOP;
}

static mnl_cb_t __cb_array[NLMSG_MIN_TYPE] = {
	[NLMSG_NOOP]	= __cb_noop,
	[NLMSG_ERROR]	= __cb_error,
	[NLMSG_DONE]	= __cb_stop,
	[NLMSG_OVERRUN]	= __cb_noop,
};

int __recv_run(struct devlink *dl, mnl_cb_t cb, void *data)
{
	int err;

	do {
		err = mnl_socket_recvfrom(dl->nl, dl->buf,
					  MNL_SOCKET_BUFFER_SIZE);
		if (err <= 0)
			break;

		err = mnl_cb_run2(dl->buf, err, dl->seq, dl->portid,
				  cb, data, __cb_array, 4);
	} while (err > 0);

	return err;
}

struct nlmsghdr *devlink_msg_prepare(struct devlink *dl, uint8_t cmd,
				     uint16_t flags)
{
	return __msg_prepare(dl, cmd, flags, dl->family, DEVLINK_GENL_VERSION);
}

int devlink_query(struct devlink *dl, struct nlmsghdr *nlh,
		  mnl_cb_t cb, void *data)
{
	int err;

	err = mnl_socket_sendto(dl->nl, nlh, nlh->nlmsg_len);
	if (err < 0)
		return err;

	return __recv_run(dl, cb, data);
}

/* static int __region_new_cb(const struct nlattr *attr, void *data) */
/* { */

/* } */

int devlink_region_dup_cb(const struct nlmsghdr *nlh, void *_region)
{
	struct nlattr *tbc[DEVLINK_ATTR_MAX + 1] = {};
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	struct devlink_region *region = _region;
	const uint8_t *chunk;
	struct nlattr *attr;
	size_t offs, len;
	uint8_t *buf;
	int err;

	devlink_parse(nlh, tb);
	if (!tb[DEVLINK_ATTR_BUS_NAME] || !tb[DEVLINK_ATTR_DEV_NAME] ||
	    !tb[DEVLINK_ATTR_REGION_CHUNKS])
		return MNL_CB_ERROR;

	mnl_attr_for_each_nested(attr, tb[DEVLINK_ATTR_REGION_CHUNKS]) {
		err = mnl_attr_parse_nested(attr, devlink_attr_cb, tbc);
		if (err != MNL_CB_OK)
			return MNL_CB_ERROR;

		attr = tbc[DEVLINK_ATTR_REGION_CHUNK_DATA];
		if (!attr)
			continue;

		chunk = mnl_attr_get_payload(attr);
		len = mnl_attr_get_payload_len(attr);

		attr = tbc[DEVLINK_ATTR_REGION_CHUNK_ADDR];
		if (!attr)
			continue;

		offs = mnl_attr_get_u64(attr);

		if (offs + len > region->size) {
			region->size = offs + len;
			buf = realloc(region->data.u8, region->size);
			if (!buf) {
				if (region->data.u8)
					free(region->data.u8);

				memset(region, 0, sizeof(*region));
				return MNL_CB_ERROR;
			}

			region->data.u8 = buf;
		}

		memcpy(region->data.u8 + offs, chunk, len);
	}

	return MNL_CB_OK;
}

static struct nlmsghdr *
__region_msg_prepare(struct devlink *dl, struct devlink_addr *addr, int port,
		     const char *name, uint8_t cmd, uint16_t flags)
{
	struct nlmsghdr *nlh;

	nlh = devlink_msg_prepare(dl, cmd, flags);

	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, addr->bus);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, addr->dev);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_REGION_NAME, name);

	if (port >= 0)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, port);

	mnl_attr_put_u32(nlh, DEVLINK_ATTR_REGION_SNAPSHOT_ID, 0xffffffff);
	return nlh;
}

int devlink_port_region_get(struct devlink *dl, struct devlink_addr *addr,
			    int port, const char *name,
			    mnl_cb_t cb, void *data)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = __region_msg_prepare(dl, addr, port, name, DEVLINK_CMD_REGION_NEW,
				   NLM_F_REQUEST | NLM_F_ACK);

	err = devlink_query(dl, nlh, NULL, NULL);
	if (err)
		return err;

	nlh = __region_msg_prepare(dl, addr, port, name, DEVLINK_CMD_REGION_READ,
				   NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP);
	err = devlink_query(dl, nlh, cb, data);

	nlh = __region_msg_prepare(dl, addr, port, name, DEVLINK_CMD_REGION_DEL,
				   NLM_F_REQUEST | NLM_F_ACK);
	devlink_query(dl, nlh, cb, data);
	return err;
}

int devlink_region_get(struct devlink *dl, struct devlink_addr *addr,
		       const char *name, mnl_cb_t cb, void *data)
{
	return devlink_port_region_get(dl, addr, -1, name, cb, data);
}

static int get_family_id_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, CTRL_ATTR_MAX) < 0)
		return MNL_CB_ERROR;

	if (type == CTRL_ATTR_FAMILY_ID &&
	    mnl_attr_validate(attr, MNL_TYPE_U16) < 0)
		return MNL_CB_ERROR;
	tb[type] = attr;
	return MNL_CB_OK;
}

static int get_family_id_cb(const struct nlmsghdr *nlh, void *data)
{
	uint32_t *p_id = data;
	struct nlattr *tb[CTRL_ATTR_MAX + 1] = {};
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);

	mnl_attr_parse(nlh, sizeof(*genl), get_family_id_attr_cb, tb);
	if (!tb[CTRL_ATTR_FAMILY_ID])
		return MNL_CB_ERROR;
	*p_id = mnl_attr_get_u16(tb[CTRL_ATTR_FAMILY_ID]);
	return MNL_CB_OK;
}

int devlink_open(struct devlink *dl)
{
	struct nlmsghdr *nlh;
	int err;

	err = ENOMEM;
	dl->buf = malloc(MNL_SOCKET_BUFFER_SIZE);
	if (!dl->buf)
		goto err;

	err = EIO;
	dl->nl = mnl_socket_open(NETLINK_GENERIC);
	if (!dl->nl)
		goto err_free;

	err = mnl_socket_bind(dl->nl, 0, MNL_SOCKET_AUTOPID);
	if (err < 0)
		goto err_close;

	dl->portid = mnl_socket_get_portid(dl->nl);

	nlh = __msg_prepare(dl, CTRL_CMD_GETFAMILY,
			    NLM_F_REQUEST | NLM_F_ACK, GENL_ID_CTRL, 1);
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, DEVLINK_GENL_NAME);

	err = mnl_socket_sendto(dl->nl, nlh, nlh->nlmsg_len);
	if (err < 0)
		goto err_close;

	err = __recv_run(dl, get_family_id_cb, &dl->family);
	if (err < 0)
		goto err_close;

	return 0;

err_close:
	mnl_socket_close(dl->nl);
err_free:
	free(dl->buf);
err:
	return err;
}
