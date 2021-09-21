/**
 * main.c - BK1785B Control Tool
 *
 * Copyright (C) 2010 Felipe Balbi <balbi@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/stat.h>

#define __unused	__attribute__ ((unused))

#define BK1785_SET_REMOTE_CONTROL_MODE	0x20
#define BK1785_SET_OUTPUT_POWER		0x21
#define BK1785_SET_MAX_OUTPUT_VOLTAGE	0x22	/* in mV */
#define BK1785_SET_OUTPUT_VOLTAGE	0x23	/* in mV */
#define BK1785_SET_OUTPUT_CURRENT	0x24	/* in mA */
#define BK1785_SET_COMM_ADDR		0x25
#define BK1785_READ			0x26
#define BK1785_CALIB_MODE		0x27
#define BK1785_READ_CALIB_STATE		0x28
#define BK1785_CALIB_VOLTAGE		0x29
#define BK1785_SEND_ACTUAL_VOLTAGE	0x2a
#define BK1785_CALIB_CURRENT		0x2b
#define BK1785_SEND_ACTUAL_CURRENT	0x2c
#define BK1785_SAVE_CALIB_DATA		0x2d
#define BK1785_SET_CALIB_INFO		0x2e
#define BK1785_READ_CALIB_INFO		0x2f
#define BK1785_READ_PRODUCT_INFO	0x31
#define BK1785_RESTORE_FACTORY_DEFAULT	0x32
#define BK1785_ENABLE_LOCAL_KEY		0x37
#define BK1785_RET_INFO_CMD		0x12	/* what ?? */

/* Remote Control Mode */
#define BK1785_FRONT_PANEL_CONTROL	0x00
#define BK1785_REMOTE_CONTROL		0x01

/* Output Power */
#define BK1785_OUTPUT_OFF		0x00
#define BK1785_OUTPUT_ON		0x01

/* Read */
#define BK1785_STATE_OUTPUT		(1 << 0)
#define BK1785_STATE_HEAT		(1 << 1)
#define BK1785_STATE_MODE		(3 << 2)
#define BK1795_STATE_FAN_SPEED		(7 << 4)
#define BK1785_STATE_OPERATION		(1 << 7)

/* Calibration Mode */
#define BK1785_CALIB_PASSWORD		(0x0128)

#define BK1785_CALIB_PROTECTION_STATE	(1 << 0)

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#define OPTION(n, h, v)			\
{					\
	.name		= #n,		\
	.has_arg	= h,		\
	.val		= v,		\
}

struct bk1785_dev {
	int		fd;
	int		addr;
};

struct bk1785_packet {
	uint8_t		def;	/* always 0xaa */
	uint8_t		addr;
	uint8_t		cmd;
	uint8_t		data[22];
	uint8_t		checksum;
} __attribute__ ((packed));

struct bk1785_read_state {
	uint16_t	pres_current;

	uint32_t	pres_voltage;

	uint8_t		state;
	uint8_t		low;
	uint8_t		high;

	uint32_t	max_voltage;
	uint32_t	voltage;

	uint8_t		reserved[5];
} __attribute__ ((packed));

struct bk1785_product_info {
	uint8_t		model[5];
	uint8_t		patchlevel;
	uint8_t		version;
	uint8_t		serial[10];

	uint8_t		reserved[5];
} __attribute__ ((packed));

enum bk1785_status {
	BK1785_COMMAND_SUCCESSFUL	= 0x80,
	BK1785_CHECKSUM_INCORRECT	= 0x90,
	BK1785_PARAMETER_INCORRECT	= 0xa0,
	BK1785_UNRECOGNIZED_COMMAND	= 0xb0,
	BK1785_INVALID_COMMAND		= 0xc0,
};

static void __unused hexdump(void *data, int size)
{
	uint8_t		*buf = data;
	int		i;

	for (i = 0; i < size; i++) {
		if (i && ((i % 8) == 0))
			printf("\n");
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

static inline void bk1785_checksum(struct bk1785_packet *pack)
{
	uint8_t		*data = (uint8_t *) pack;
	int		cksum = 0;
	int		i;

	for (i = 0; i < sizeof(*pack) - 1; i++) {
		uint8_t			tmp;

		tmp = data[i];
		cksum += tmp;
		cksum %= 256;
	}

	pack->checksum = cksum;
}

static int bk1785_read(struct bk1785_dev *bk, struct bk1785_packet *pack)
{
	struct pollfd	pfd;

	unsigned	done = 0;
	int		ret;

	uint8_t		buf[26];

	/* start polling for data available */
	pfd.fd = bk->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	ret = poll(&pfd, 1, -1);
	if (ret <= 0) {
		fprintf(stderr, "poll failed\n");
		return ret;
	}

	while (done < sizeof(buf)) {
		ret = read(bk->fd, buf + done, sizeof(buf));
		if (ret < 0) {
			perror("failed to read");
			return ret;
		}

		done += ret;
	}

	memcpy(pack, buf, sizeof(buf));

	return 0;
}

static int bk1785_write(struct bk1785_dev *bk, struct bk1785_packet *pack)
{
	struct pollfd	pfd;

	unsigned	done = 0;
	int		ret;

	uint8_t		buf[26];

	bk1785_checksum(pack);

	memcpy(buf, pack, sizeof(buf));

	/* start polling for writing data */
	pfd.fd = bk->fd;
	pfd.events = POLLOUT;
	pfd.revents = 0;

	ret = poll(&pfd, 1, -1);
	if (ret <= 0) {
		fprintf(stderr, "poll failed\n");
		return ret;
	}

	while (done < sizeof(buf)) {
		ret = write(bk->fd, buf + done, 1);
		if (ret < 0) {
			perror("failed to write");
			return ret;
		}

		done += 1;
	}

	return 0;
}

static int bk1785_send_command(struct bk1785_dev *bk, uint8_t cmd, uint8_t *data)
{
	struct bk1785_read_state	*state;
	struct bk1785_product_info	*info;
	struct bk1785_packet		p;

	int				status;
	int				ret = 0;

	memset(&p, 0x00, sizeof(p));

	p.def	= 0xaa;	/* first byte is always 0xaa */
	p.addr	= bk->addr;
	p.cmd	= cmd;
	memcpy(p.data, data, 22);

	ret = bk1785_write(bk, &p);
	if (ret)
		return -1;

	/* split this into send_command and read_status calls */
	memset(&p, 0x00, sizeof(p));

	ret = bk1785_read(bk, &p);
	if (ret)
		return -1;

	switch (cmd) {
	case BK1785_READ:
		state = (struct bk1785_read_state *) p.data;

		fprintf(stdout, "Present Output Current %d\n", state->pres_current);
		fprintf(stdout, "Present Output Voltage %d\n", state->pres_voltage);
		fprintf(stdout, "Power Supply State %02x\n", state->state);
		fprintf(stdout, "Low Byte of current value %02x\n", state->low);
		fprintf(stdout, "High Byte of current value %02x\n", state->high);
		fprintf(stdout, "Max Output Voltage %d\n", state->max_voltage);
		fprintf(stdout, "Output Voltage %d\n", state->voltage);

		break;
	case BK1785_READ_PRODUCT_INFO:
		{
			info = (struct bk1785_product_info *) p.data;

			fprintf(stdout, "Model %s FW Version %d.%d Serial Number %s\n",
					info->model, info->version, info->patchlevel,
					info->serial);
		}
		break;
	default:
		status = p.data[0];
		switch (status) {
		case BK1785_COMMAND_SUCCESSFUL:
			printf("Command Successful\n");
			ret = 0;
			memcpy(data, p.data, 22);
			break;

		case BK1785_CHECKSUM_INCORRECT:
			printf("Checksum is incorrect\n");
			ret = -1;
			break;

		case BK1785_PARAMETER_INCORRECT:
			printf("Parameter is incorrect\n");
			ret = -1;
			break;

		case BK1785_UNRECOGNIZED_COMMAND:
			printf("Unrecognized Command\n");
			ret = -1;
			break;

		case BK1785_INVALID_COMMAND:
			printf("Invalid Command\n");
			ret = -1;
			break;
		default:
			printf("UNKNOWN status (%02x)\n", status);
			ret = -1;
			break;
		}
	}

	return ret;
}

static struct option bk_opts[] = {
	OPTION("terminal",		1,	't'),
	OPTION("set-remote",		1,	'r'),
	OPTION("set-voltage",		1,	'v'),
	OPTION("set-current",		1,	'c'),
	OPTION("set-output",		1,	'o'),
	OPTION("read-state",		0,	's'),
	OPTION("model",			0,	'm'),
	{  }	/* Terminating Entry */
};

static void usage(char *cmd)
{
	fprintf(stderr, "%s: -t /dev/ttyUSB0 [-m] [-s] [-r 1/0]\
			[-v voltage] [-c current] [-o 1/0]\n", cmd);
}

int main(int argc, char *argv[])
{
	struct termios		term;

	struct bk1785_dev	*bk;

	unsigned		cmd = 0;

	int			ret = -1;
	int			fd;

	uint8_t			data[22];
	char			*tty = NULL;

	memset(&term, 0x00, sizeof(term));
	memset(data, 0x00, sizeof(data));

	while (ARRAY_SIZE(bk_opts)) {
		int			tmp = 0;
		int			opt;

		opt = getopt_long(argc, argv, "t:r:v:c:o:sm", bk_opts, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 't':
			tty = optarg;
			break;
		case 'r':
			cmd = BK1785_SET_REMOTE_CONTROL_MODE;
			tmp = atoi(optarg);

			memcpy(data, &tmp, sizeof(tmp));
			break;
		case 'v':
			cmd = BK1785_SET_OUTPUT_VOLTAGE;
			tmp = atoi(optarg);

			memcpy(data, &tmp, sizeof(tmp));
			break;
		case 'c':
			cmd = BK1785_SET_OUTPUT_CURRENT;
			tmp = atoi(optarg);

			memcpy(data, &tmp, sizeof(tmp));
			break;
		case 'o':
			cmd = BK1785_SET_OUTPUT_POWER;
			tmp = atoi(optarg);

			memcpy(data, &tmp, sizeof(tmp));
			break;
		case 's':
			cmd = BK1785_READ;
			break;
		case 'm':
			cmd = BK1785_READ_PRODUCT_INFO;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	bk = malloc(sizeof(*bk));
	if (!bk) {
		fprintf(stderr, "not enough memory\n");
		goto out0;
	}

	fd = open(tty, O_RDWR);
	if (fd < 0)
		goto out1;

	tcdrain(fd);
	tcflush(fd, TCIOFLUSH);

	tcgetattr(fd, &term);
	cfmakeraw(&term);
	cfsetspeed(&term, B9600);
	tcsetattr(fd, TCSANOW, &term);

	/* Wait 100ms for changes to happen */
	usleep(100000);

	bk->fd = fd;
	bk->addr = 0;

	ret = bk1785_send_command(bk, cmd, data);
	if (ret)
		goto out2;

out2:
	close(fd);

out1:
	free(bk);

out0:
	return ret;
}

