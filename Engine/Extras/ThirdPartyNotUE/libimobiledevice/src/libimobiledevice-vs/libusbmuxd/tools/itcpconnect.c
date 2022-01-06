/*
 * itcpconnect.c - modified version of iproxy that directly connects to a tcp socket and sends data from stdin/stdout.
 * 
 *
 * Copyright (C) 2009-2020 Nikias Bassen <nikias@gmx.li>
 * Copyright (C) 2014      Martin Szulecki <m.szulecki@libimobiledevice.org>
 * Copyright (C) 2009      Paul Sladen <libiphone@paul.sladen.org>
 *
 * Based upon iTunnel source code, Copyright (c) 2008 Jing Su.
 * http://www.cs.toronto.edu/~jingsu/itunnel/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stddef.h>
#include <errno.h>
//#include <getopt.h>
#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
typedef unsigned int socklen_t;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#endif
#include "socket.h"
#include "usbmuxd.h"

#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif

static int debug_level = 0;

static uint16_t device_port = 0;
static char* device_udid = NULL;
static enum usbmux_lookup_options lookup_opts = 0;

struct client_data {
	int sfd;
	volatile int stop_ctos;
	volatile int stop_stoc;
#ifdef WIN32
	HANDLE ctos;
#else
	pthread_t ctos;
#endif
};

#define USBMUXD_SOCKET_PORT 27015

static void *run_stoc_loop(void *arg)
{
	struct client_data *cdata = (struct client_data*)arg;
	int recv_len;
	char buffer[131072];

	while (!cdata->stop_stoc && cdata->sfd > 0) {
		recv_len = socket_receive_timeout(cdata->sfd, buffer, sizeof(buffer), 0, 5000);
		if (recv_len <= 0) {
			if (recv_len == 0 || recv_len == -ETIMEDOUT) {
				// try again
				continue;
			} else {
				fprintf(stderr, "recv failed: %s\n", strerror(-recv_len));
				break;
			}
		} else {
			// send to stdout
			fwrite(buffer, 1, recv_len, stdout);
		}
	}

	cdata->stop_ctos = 1;

#ifdef WIN32
	CancelSynchronousIo(cdata->ctos);
#else
	close(0);
#endif

	return NULL;
}

static void *run_ctos_loop(void *arg)
{
	struct client_data *cdata = (struct client_data*)arg;
	int recv_len;
	int sent;
	char buffer[131072];
#ifdef WIN32
	HANDLE stoc = NULL;
#else
	pthread_t stoc;
#endif

	cdata->stop_stoc = 0;
#ifdef WIN32
	stoc = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)run_stoc_loop, cdata, 0, NULL);
#else
	pthread_create(&stoc, NULL, run_stoc_loop, cdata);
#endif

	while (!cdata->stop_ctos && cdata->sfd > 0) {
#ifdef WIN32
		if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &recv_len, 0)) {
			fprintf(stderr, "ReadFile from stdin failed: %d\n", GetLastError());
			break;
		}
#else
		recv_len = read(0, buffer, sizeof(buffer));
#endif
		if (recv_len == 0) {
			// end of file
			break;
		} else if (recv_len < 0) {
			fprintf(stderr, "stdin fread failed: %s\n", strerror(-recv_len));
			break;
		} else {
			// send to local socket
			fprintf(stderr, "sending %d bytes >>", recv_len);
			fwrite(buffer, 1, recv_len, stderr);
			fprintf(stderr, "<<\n");
			sent = socket_send(cdata->sfd, buffer, recv_len);
			if (sent < recv_len) {
				if (sent <= 0) {
					fprintf(stderr, "send failed: %s\n", strerror(errno));
					break;
				} else {
					fprintf(stderr, "only sent %d from %d bytes\n", sent, recv_len);
				}
			}
		}
	}
	cdata->stop_stoc = 1;

#ifdef WIN32
	WaitForSingleObject(stoc, INFINITE);
#else
	pthread_join(stoc, NULL);
#endif

	return NULL;
}

static void *acceptor_thread()
{
	usbmuxd_device_info_t *dev_list = NULL;
	usbmuxd_device_info_t *dev = NULL;
	usbmuxd_device_info_t muxdev;

	int count;

	if (device_udid) {
		if (usbmuxd_get_device(device_udid, &muxdev, lookup_opts) > 0) {
			dev = &muxdev;
		}
	} else {
		if ((count = usbmuxd_get_device_list(&dev_list)) < 0) {
			fprintf(stderr, "Connecting to usbmuxd failed, terminating.\n");
			free(dev_list);
			return NULL;
		}

		if (dev_list == NULL || dev_list[0].handle == 0) {
			fprintf(stderr, "No connected device found, terminating.\n");
			free(dev_list);
			return NULL;
		}

		int i;
		for (i = 0; i < count; i++) {
			if (dev_list[i].conn_type == CONNECTION_TYPE_USB && (lookup_opts & DEVICE_LOOKUP_USBMUX)) {
				dev = &(dev_list[i]);
				break;
			} else if (dev_list[i].conn_type == CONNECTION_TYPE_NETWORK && (lookup_opts & DEVICE_LOOKUP_NETWORK)) {
				dev = &(dev_list[i]);
				break;
			}
		}
	}

	if (dev == NULL || dev->handle == 0) {
		fprintf(stderr, "No connected/matching device found, disconnecting client.\n");
		free(dev_list);
		return NULL;
	}

	struct client_data cdata;

	cdata.sfd = -1;
	if (dev->conn_type == CONNECTION_TYPE_NETWORK) {
		unsigned char saddr_[32];
		memset(saddr_, '\0', sizeof(saddr_));
		struct sockaddr* saddr = (struct sockaddr*)&saddr_[0];
		if (((char*)dev->conn_data)[1] == 0x02) { // AF_INET
			saddr->sa_family = AF_INET;
			memcpy(&saddr->sa_data[0], (char*)dev->conn_data+2, 14);
		}
		else if (((char*)dev->conn_data)[1] == 0x1E) { //AF_INET6 (bsd)
#ifdef AF_INET6
			saddr->sa_family = AF_INET6;
			memcpy(&saddr->sa_data[0], (char*)dev->conn_data+2, 26);
#else
			fprintf(stderr, "ERROR: Got an IPv6 address but this system doesn't support IPv6\n");
			return NULL;
#endif
		}
		else {
			fprintf(stderr, "Unsupported address family 0x%02x\n", ((char*)dev->conn_data)[1]);
			return NULL;
		}
		char addrtxt[48];
		addrtxt[0] = '\0';
		if (!socket_addr_to_string(saddr, addrtxt, sizeof(addrtxt))) {
			fprintf(stderr, "Failed to convert network address: %d (%s)\n", errno, strerror(errno));
		}
		fprintf(stdout, "Requesting connecion to NETWORK device %s (serial: %s), port %d\n", addrtxt, dev->udid, device_port);
		cdata.sfd = socket_connect_addr(saddr, device_port);
	} else if (dev->conn_type == CONNECTION_TYPE_USB) {
		fprintf(stdout, "Requesting connecion to USB device handle %d (serial: %s), port %d\n", dev->handle, dev->udid, device_port);

		cdata.sfd = usbmuxd_connect(dev->handle, device_port);
	}
	free(dev_list);
	if (cdata.sfd < 0) {
		fprintf(stderr, "Error connecting to device: %s\n", strerror(-cdata.sfd));
	} else {
		cdata.stop_ctos = 0;

#ifdef WIN32
		cdata.ctos = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)run_ctos_loop, &cdata, 0, NULL);
		WaitForSingleObject(cdata.ctos, INFINITE);
#else
		pthread_create(&cdata.ctos, NULL, run_ctos_loop, &cdata);
		pthread_join(cdata.ctos, NULL);
#endif
	}

	if (cdata.sfd > 0) {
		socket_close(cdata.sfd);
	}

	return NULL;
}

static void print_usage(int argc, char **argv, int is_error)
{
	char *name = NULL;
	name = strrchr(argv[0], '/');
	fprintf(is_error ? stderr : stdout, "Usage: %s [OPTIONS] DEVICE_PORT\n", (name ? name + 1: argv[0]));
	fprintf(is_error ? stderr : stdout,
	  "Connect to TCP service on remote iOS device to stdin/stdout.\n\n" \
	  "  -u, --udid UDID    target specific device by UDID\n" \
	  "  -n, --network      connect to network device\n" \
	  "  -l, --local        connect to USB device (default)\n" \
	  "  -h, --help         prints usage information\n" \
	  "  -d, --debug        increase debug level\n" \
	  "\n" \
	  "Homepage: <" PACKAGE_URL ">\n"
	  "Bug reports: <" PACKAGE_BUGREPORT ">\n"
	  "\n"
	);
}

int main(int argc, char **argv)
{
	int arg;
	for (arg = 1; arg < argc; arg++) {
		if (!strcmp(argv[arg], "-d") || !strcmp(argv[arg], "--debug")) {
			libusbmuxd_set_debug_level(++debug_level);
			continue;
		}
		else if (!strcmp(argv[arg], "-u") || !strcmp(argv[arg], "--udid")) {
			arg++;
			if (!argv[arg] || !*argv[arg]) {
				fprintf(stderr, "ERROR: UDID must not be empty!\n");
				print_usage(argc, argv, 1);
				return 2;
			}
			free(device_udid);
			device_udid = strdup(argv[arg]);
			continue;
		}
		else if (!strcmp(argv[arg], "-n") || !strcmp(argv[arg], "--network")) {
			lookup_opts |= DEVICE_LOOKUP_NETWORK;
			continue;
		}
		else if (!strcmp(argv[arg], "-l") || !strcmp(argv[arg], "--local")) {
			lookup_opts |= DEVICE_LOOKUP_USBMUX;
			continue;
		}
		else if (!strcmp(argv[arg], "-h") || !strcmp(argv[arg], "--help")) {
			print_usage(argc, argv, 0);
			return 0;
		}
		else if (argv[arg][0] == '-') {
			print_usage(argc, argv, 1);
			return 2;
		}
		else
		{
			break;
		}
	}

	if (lookup_opts == 0) {
		lookup_opts = DEVICE_LOOKUP_USBMUX;
	}

	argc -= arg;
	argv += arg;

	if (argc < 1) {
		print_usage(argc - arg, argv - arg, 1);
		free(device_udid);
		return 2;
	}

	device_port = atoi(argv[0]);

	if (!device_port) {
		fprintf(stderr, "Invalid device port specified!\n");
		free(device_udid);
		return -EINVAL;
	}

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	acceptor_thread();

	free(device_udid);

	fprintf(stderr, "Exiting.\n");

	return 0;
}
