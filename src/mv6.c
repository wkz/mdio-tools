#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mdio.h>

#include "mdio.h"

#define BIT(_n) (1 << (_n))

#define MV6_CMD  0
#define MV6_CMD_BUSY BIT(15)
#define MV6_CMD_C22  BIT(12)

#define MV6_DATA 1

#define MV6_G1 0x1b
#define MV6_G2 0x1c

struct mv6_mdio_ops {
	uint16_t sw;

	struct mdio_ops ops;
};
#define to_mv6_mdio_ops(_ops) container_of(_ops, struct mv6_mdio_ops, ops)

static inline uint16_t mv6_multi_cmd(uint8_t dev, uint8_t reg, bool write)
{
	return MV6_CMD_BUSY | MV6_CMD_C22 | ((write ? 1 : 2) << 10)|
		(dev << 5) | reg;
}

static void mv6_push_wait_cmd(struct mdio_prog *prog, uint8_t sw)
{
	int retry;

	retry = prog->len;
	mdio_prog_push(prog, INSN(READ, IMM(sw), IMM(MV6_CMD), REG(0)));
	mdio_prog_push(prog, INSN(AND, REG(0), IMM(MV6_CMD_BUSY), REG(0)));
	mdio_prog_push(prog, INSN(JEQ, REG(0), IMM(MV6_CMD_BUSY),
				  GOTO(prog->len, retry)));
}

static void mv6_push_read_to(struct mdio_ops *ops, struct mdio_prog *prog,
			     uint8_t dev, uint8_t reg, uint8_t to)
{
	struct mv6_mdio_ops *mops = to_mv6_mdio_ops(ops);

	if (!mops->sw) {
		/* Single-chip addressing, the switch uses the entire
		 * underlying bus */
		mdio_prog_push(prog, INSN(READ, IMM(dev), IMM(reg), REG(to)));
		return;
	}

	mdio_prog_push(prog, INSN(WRITE, IMM(mops->sw), IMM(MV6_CMD),
				  IMM(mv6_multi_cmd(dev, reg, false))));
	mv6_push_wait_cmd(prog, mops->sw);
	mdio_prog_push(prog, INSN(READ, IMM(mops->sw), IMM(MV6_DATA), REG(to)));
}

static int mv6_push_read(struct mdio_ops *ops, struct mdio_prog *prog,
			  uint16_t dev, uint16_t reg)
{
	mv6_push_read_to(ops, prog, dev, reg, 0);
	return 0;
}

static void mv6_push_wait_bit(struct mdio_ops *ops, struct mdio_prog *prog,
			      uint8_t dev, uint8_t reg, uint32_t mask)
{
	int retry;

	retry = prog->len;
	mv6_push_read(ops, prog, dev, reg);
	mdio_prog_push(prog, INSN(AND, REG(0), mask, REG(0)));
	mdio_prog_push(prog, INSN(JEQ, REG(0), mask,
				  GOTO(prog->len, retry)));
}

static int mv6_push_write(struct mdio_ops *ops, struct mdio_prog *prog,
			  uint16_t dev, uint16_t reg, uint32_t val)
{
	struct mv6_mdio_ops *mops = to_mv6_mdio_ops(ops);

	if (!mops->sw) {
		/* Single-chip addressing, the switch uses the entire
		 * underlying bus */
		mdio_prog_push(prog, INSN(WRITE, IMM(dev), IMM(reg), val));
		return 0;
	}

	mdio_prog_push(prog, INSN(WRITE, IMM(mops->sw), IMM(MV6_DATA), val));
	mdio_prog_push(prog, INSN(WRITE, IMM(mops->sw), IMM(MV6_CMD),
				  IMM(mv6_multi_cmd(dev, reg, true))));
	mv6_push_wait_cmd(prog, mops->sw);
	return 0;
}


static void mv6_usage(FILE *fp)
{
	fputs("Usage: mdio mv6 COMMAND OPTIONS\n"
	      "\n"
 	      "  atu BUS SWITCH DEV FID\n"
	      "    Dump ATU entries from FID.\n"
	      "\n"
 	      "  dump BUS SWITCH DEV [REG[-REG|+NUM]]\n"
	      "    Dump multiple registers. If no range is specified, all registers of the\n"
	      "    selected device are dumped.\n"
	      "\n"
	      "  help\n"
	      "    Show this usage message.\n"
	      "\n"
 	      "  raw BUS SWITCH DEV REG [VAL[/MASK]]\n"
	      "    Raw register access. If SWITCH is 0, single-chip addressing is used, all\n"
	      "    other addresses will use multi-chip addressing. DEV, REG and VAL/MASK\n"
	      "    operate like in 'mdio raw'.\n"
	      "\n"
 	      "  vtu BUS SWITCH DEV\n"
	      "    Dump VTU entries.\n",
	      fp);
}

static int mv6_help_exec(int argc, char **argv)
{
	mv6_usage(stdout);
	return 0;
}


int mv6_lag_cb(uint32_t *data, int len, int err, void *_null)
{
	uint32_t agg = 0x7ff;
	int lag, mask, port;

	if (len != 1 + 8 + 16)
		return 1;

	printf("LAG Masks (Hash:%s):\n", data[0] ? "Yes" : "No");

	puts("\e[7m  0  1  2  3  4  5  6  7  8  9  a\e[0m");

	for (mask = 0; mask < 8; mask++)
		agg &= data[1 + mask];

	for (mask = 0; mask < 8; mask++) {
		for (port = 0; port < 11; port++) {
			if (BIT(port) & agg)
				fputs("  |", stdout);
			else if (BIT(port) & data[1 + mask])
				printf("  %d", mask);
			else
				fputs("  .", stdout);
		}
		putchar('\n');
	}

	puts("\nLAG Maps:");

	data = &data[1 + 8];

	puts("\e[7mID  0  1  2  3  4  5  6  7  8  9  a\e[0m");
	for (lag = 0; lag < 16; lag++) {
		if (!(data[lag] & 0x7ff))
			continue;

		printf("%2d", lag);
		for (port = 0; port < 11; port++) {
			if (BIT(port) & data[lag])
				printf("  %x", port);
			else
				fputs("  .", stdout);
		}
		putchar('\n');
	}

	return 0;
}

static int mv6_lag_exec(int argc, char **argv)
{
	struct mv6_mdio_ops mops = {
		.ops = {
			.usage = mv6_usage,
			.push_read = mv6_push_read,
			.push_write = mv6_push_write,
		}
	};
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	int get_next, err;

	if (argc < 3) {
		mv6_usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &mops.ops.bus))
		return 1;

	if (mdio_parse_dev(argv[2], &mops.sw, false))
		return 1;

	if (mops.sw)
		mv6_push_wait_cmd(&prog, mops.sw);

	/* LAG masks. Read out hash bit to r1 and emit */
	mv6_push_read_to(&mops.ops, &prog, MV6_G2, 0x7, 1);
	mdio_prog_push(&prog, INSN(AND, REG(1), IMM(BIT(11)), REG(1)));
	mdio_prog_push(&prog, INSN(EMIT, REG(1), 0, 0));
	get_next = prog.len;
	/* Read and emit current mask */
	mv6_push_write(&mops.ops, &prog, MV6_G2, 0x7, REG(1));
	mv6_push_read(&mops.ops, &prog, MV6_G2, 0x7);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	/* Move to next mask */
	mdio_prog_push(&prog, INSN(ADD, REG(1), IMM(0x1000), REG(1)));
	mdio_prog_push(&prog, INSN(AND, REG(1), IMM(0x7000), REG(2)));
	mdio_prog_push(&prog, INSN(JNE, REG(2), IMM(0), GOTO(prog.len, get_next)));

	/* LAG maps. */
	mdio_prog_push(&prog, INSN(ADD, IMM(0), IMM(0), REG(1)));
	get_next = prog.len;
	/* Read and emit current map */
	mv6_push_write(&mops.ops, &prog, MV6_G2, 0x8, REG(1));
	mv6_push_read(&mops.ops, &prog, MV6_G2, 0x8);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	/* Move to next map */
	mdio_prog_push(&prog, INSN(ADD, REG(1), IMM(0x0800), REG(1)));
	mdio_prog_push(&prog, INSN(JNE, REG(1), IMM(0x8000), GOTO(prog.len, get_next)));

	err = mdio_xfer(mops.ops.bus, &prog, mv6_lag_cb, NULL);
	free(mops.ops.bus);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: LAG operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}



int mv6_atu_cb(uint32_t *data, int len, int err, void *_null)
{
	int port, state;

	puts("\e[7mADDRESS            0  1  2  3  4  5  6  7  8  9  a  STATE      \e[0m");

	for (; len >= 4; len -= 4, data += 4) {
		printf("%02x:%02x:%02x:%02x:%02x:%02x",
		       data[1] >> 8, data[1] & 0xff,
		       data[2] >> 8, data[2] & 0xff,
		       data[3] >> 8, data[3] & 0xff);

		if (BIT(15) & data[0]) {
			printf("  lag%-2u                          ",
			       (data[0] >> 4) & 0x1f);
		} else {
			for (port = 0; port < 11; port++) {
				if (BIT(port + 4) & data[0])
					printf("  %x", port);
				else
					fputs("  .", stdout);
			}
		}

		state = data[0] & 0xf;
		if (BIT(8) & data[1]) {
			switch (state) {
			case 0x0:
				fputs("  Unused", stdout);
				break;
			case 0x4:
				fputs("  Policy", stdout);
				break;
			case 0x5:
				fputs("  AVB/NRL", stdout);
				break;
			case 0x6:
				fputs("  MGMT", stdout);
				break;
			case 0x7:
				fputs("  Static", stdout);
				break;
			case 0xc:
				fputs("  Policy, PO", stdout);
				break;
			case 0xd:
				fputs("  AVB/NRL, PO", stdout);
				break;
			case 0xe:
				fputs("  MGMT, PO", stdout);
				break;
			case 0xf:
				fputs("  Static, PO", stdout);
				break;
			default:
				printf("  %#x", state);
				break;
			}
		} else {
			switch (state) {
			case 0x0:
				fputs("  Unused", stdout);
				break;
			case 0x1:
			case 0x2:
			case 0x3:
			case 0x4:
			case 0x5:
			case 0x6:
			case 0x7:
				printf("  Age %d", state);
				break;
			case 0x8:
				fputs("  Policy", stdout);
				break;
			case 0x9:
				fputs("  Policy, PO", stdout);
				break;
			case 0xa:
				fputs("  AVB/NRL", stdout);
				break;
			case 0xb:
				fputs("  AVB/NRL, PO", stdout);
				break;
			case 0xc:
				fputs("  MGMT", stdout);
				break;
			case 0xd:
				fputs("  MGMT, PO", stdout);
				break;
			case 0xe:
				fputs("  Static", stdout);
				break;
			case 0xf:
				fputs("  Static, PO", stdout);
				break;
			}
		}
		putchar('\n');
	}

	if (len != 0)
		return 1;

	return err;
}

static int mv6_atu_exec(int argc, char **argv)
{
	struct mv6_mdio_ops mops = {
		.ops = {
			.usage = mv6_usage,
			.push_read = mv6_push_read,
			.push_write = mv6_push_write,
		}
	};
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	int get_next, jmp_out, err;
	uint16_t fid;

	if (argc < 4) {
		mv6_usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &mops.ops.bus))
		return 1;

	if (mdio_parse_dev(argv[2], &mops.sw, false))
		return 1;

	if (mdio_parse_val(argv[3], &fid, NULL) || fid > 0xfff)
		return 1;

	if (mops.sw)
		mv6_push_wait_cmd(&prog, mops.sw);

	mv6_push_wait_bit(&mops.ops, &prog, MV6_G1, 0xb, IMM(0x8000));
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0x1, IMM(fid));
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0xd, IMM(0xffff));
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0xe, IMM(0xffff));
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0xf, IMM(0xffff));

	get_next = prog.len;
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0xb, IMM(BIT(15) | (4 << 12)));
	mv6_push_wait_bit(&mops.ops, &prog, MV6_G1, 0xb, IMM(0x8000));

	/* Read entry */
	mv6_push_read_to(&mops.ops, &prog, MV6_G1, 0xc, 1);
	mv6_push_read_to(&mops.ops, &prog, MV6_G1, 0xd, 2);
	mv6_push_read_to(&mops.ops, &prog, MV6_G1, 0xe, 3);
	mv6_push_read_to(&mops.ops, &prog, MV6_G1, 0xf, 4);

	/* if (broadcast) r5 = 0xffff */
	mdio_prog_push(&prog, INSN(AND, REG(2), REG(3), REG(5)));
	mdio_prog_push(&prog, INSN(AND, REG(4), REG(5), REG(5)));

	/* if (broadcast && !state) break  */
	mdio_prog_push(&prog, INSN(JNE, REG(5), IMM(0xffff), IMM(2)));
	mdio_prog_push(&prog, INSN(AND, REG(1), IMM(0xf), REG(6)));
	jmp_out = prog.len;
	mdio_prog_push(&prog, INSN(JEQ, REG(6), IMM(0), INVALID));

	mdio_prog_push(&prog, INSN(EMIT, REG(1), 0, 0));
	mdio_prog_push(&prog, INSN(EMIT, REG(2), 0, 0));
	mdio_prog_push(&prog, INSN(EMIT, REG(3), 0, 0));
	mdio_prog_push(&prog, INSN(EMIT, REG(4), 0, 0));

	/* while (!broadcast)  */
	mdio_prog_push(&prog, INSN(JNE, REG(5), IMM(0xffff), GOTO(prog.len, get_next)));
	prog.insns[jmp_out].arg2 = GOTO(jmp_out, prog.len);

	err = mdio_xfer(mops.ops.bus, &prog, mv6_atu_cb, NULL);
	free(mops.ops.bus);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: ATU operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}


int mv6_vtu_cb(uint32_t *data, int len, int err, void *_null)
{
	int port, mode;

	puts("\e[7m VID   FID  SID  0  1  2  3  4  5  6  7  8  9  a  Fp Qp Ln Uc Bc Mc Pl\e[0m");

	for (; len >= 7; len -= 7, data += 7) {
		printf("%4u", data[3] & 0xfff);
		printf("  %4u", data[0] & 0xfff);
		printf("  %3u", data[1] & 0x3f);

		for (port = 0; port < 11; port++) {
			mode = (data[4 + (port >> 3)] >> (2 * (port & 0x7))) & 0x3;
			switch (mode) {
			case 0:
				fputs("  =", stdout);
				break;
			case 1:
				fputs("  U", stdout);
				break;
			case 2:
				fputs("  T", stdout);
				break;
			case 3:
				fputs("  .", stdout);
				break;
			}
		}

		if (data[5] & BIT(11))
			printf("  %u", (data[5] >> 8) & 7);
		else
			fputs("  .", stdout);

		if (data[5] & BIT(15))
			printf("  %u", (data[5] >> 12) & 7);
		else
			fputs("  .", stdout);

		fputs((data[1] & BIT(15)) ? "  N" : "  y", stdout);
		fputs((data[1] & BIT(14)) ? "  N" : "  y", stdout);
		fputs((data[1] & BIT(13)) ? "  N" : "  y", stdout);
		fputs((data[1] & BIT(12)) ? "  N" : "  y", stdout);
		fputs((data[0] & BIT(12)) ? "  Y" : "  n", stdout);

		putchar('\n');
	}

	if (len != 0)
		return 1;

	return err;
}

static int mv6_vtu_exec(int argc, char **argv)
{
	struct mv6_mdio_ops mops = {
		.ops = {
			.usage = mv6_usage,
			.push_read = mv6_push_read,
			.push_write = mv6_push_write,
		}
	};
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	int get_next, jmp_out, err;

	if (argc < 3) {
		mv6_usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &mops.ops.bus))
		return 1;

	if (mdio_parse_dev(argv[2], &mops.sw, false))
		return 1;

	if (mops.sw)
		mv6_push_wait_cmd(&prog, mops.sw);

	mv6_push_wait_bit(&mops.ops, &prog, MV6_G1, 0x5, IMM(0x8000));
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0x6, IMM(0));

	get_next = prog.len;
	mv6_push_write(&mops.ops, &prog, MV6_G1, 0x5, IMM(BIT(15) | (4 << 12)));
	mv6_push_wait_bit(&mops.ops, &prog, MV6_G1, 0x5, IMM(0x8000));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x6);
	mdio_prog_push(&prog, INSN(AND, REG(0), IMM(0x1000), REG(0)));
	jmp_out = prog.len;
	mdio_prog_push(&prog, INSN(JNE, REG(0), IMM(0x1000), INVALID));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x2);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x3);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x5);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x6);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x7);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x8);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mv6_push_read(&mops.ops, &prog, MV6_G1, 0x9);
	mdio_prog_push(&prog, INSN(EMIT, REG(0), 0, 0));
	mdio_prog_push(&prog, INSN(JEQ, IMM(0), IMM(0), GOTO(prog.len, get_next)));
	prog.insns[jmp_out].arg2 = GOTO(jmp_out, prog.len);

	err = mdio_xfer(mops.ops.bus, &prog, mv6_vtu_cb, NULL);
	free(mops.ops.bus);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: VTU operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}


int mv6_dump_exec(int argc, char **argv)
{
	struct mv6_mdio_ops mops = {
		.ops = {
			.usage = mv6_usage,
			.push_read = mv6_push_read,
			.push_write = mv6_push_write,
		},
	};

	if (argc < 3) {
		mv6_usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &mops.ops.bus))
		return 1;

	if (mdio_parse_dev(argv[2], &mops.sw, false))
		return 1;

	return mdio_dump_exec(&mops.ops, argc - 3, &argv[3]);
}

int mv6_raw_exec(int argc, char **argv)
{
	struct mv6_mdio_ops mops = {
		.ops = {
			.usage = mv6_usage,
			.push_read = mv6_push_read,
			.push_write = mv6_push_write,
		},
	};

	if (argc < 3) {
		mv6_usage(stderr);
		return 1;
	}

	if (mdio_parse_bus(argv[1], &mops.ops.bus))
		return 1;

	if (mdio_parse_dev(argv[2], &mops.sw, false))
		return 1;

	return mdio_raw_exec(&mops.ops, argc - 3, &argv[3]);
}

static const struct cmd mv6_cmds[] = {
	{ .name = "help", .exec = mv6_help_exec },
	{ .name = "lag",  .exec = mv6_lag_exec  },
	{ .name = "atu",  .exec = mv6_atu_exec  },
	{ .name = "vtu",  .exec = mv6_vtu_exec  },
	{ .name = "dump", .exec = mv6_dump_exec },
	{ .name = "raw",  .exec = mv6_raw_exec  },

	{ .name = NULL}
};


static int mv6_exec(int argc, char **argv)
{
	const struct cmd *cmd;

	if (argc <= 1)
		goto err;

	for (cmd = mv6_cmds; cmd->name; cmd++) {
		if (!strcmp(argv[1], cmd->name))
			return cmd->exec(argc - 1, &argv[1]);
	}

err:
	mv6_usage(stderr);
	return 1;
}
DEFINE_CMD(mv6, mv6_exec);
