/* Daemon and client IPC API
 *
 * Copyright (C) 2001-2005  Carsten Schill <carsten@cschill.de>
 * Copyright (C) 2006-2009  Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2009       Todd Hayton <todd.hayton@gmail.com>
 * Copyright (C) 2009-2011  Micha Lenk <micha@debian.org>
 * Copyright (C) 2011-2013  Joachim Nilsson <troglobit@gmail.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "mclab.h"

#define SOCKET_PATH "/var/run/smcroute"

/* server's listen socket */
static int server_sd = -1;

/* connected server or client socket */
static int client_sd = -1;

/**
 * ipc_server_init - Initialise an IPC server socket
 *
 * Returns:
 * The socket descriptor, or -1 on error with @errno set.
 */
int ipc_server_init(void)
{
	struct sockaddr_un sa;
	socklen_t len;

	if (server_sd >= 0)
		close(server_sd);

	server_sd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (server_sd < 0)
		return -1;

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sa.sun_len = 0;	/* <- correct length is set by the OS */
#endif
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, SOCKET_PATH);

	unlink(SOCKET_PATH);

	len = offsetof(struct sockaddr_un, sun_path) + strlen(SOCKET_PATH);
	if (bind(server_sd, (struct sockaddr *)&sa, len) < 0 || listen(server_sd, 1)) {
		int err = errno;

		close(server_sd);
		server_sd = -1;
		errno = err;
	}

	return server_sd;
}

/**
 * ipc_client_init - Connects to the IPC socket of the server
 *
 * Returns:
 * POSIX OK(0) on success, non-zero on error with @errno set.
 * Typically %EACCES, %ENOENT, or %ECONREFUSED.
 */
int ipc_client_init(void)
{
	struct sockaddr_un sa;
	socklen_t len;

	if (client_sd >= 0)
		close(client_sd);

	client_sd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (client_sd < 0)
		return -1;

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sa.sun_len = 0;	/* <- correct length is set by the OS */
#endif
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, SOCKET_PATH);

	len = offsetof(struct sockaddr_un, sun_path) + strlen(SOCKET_PATH);
	if (connect(client_sd, (struct sockaddr *)&sa, len) < 0) {
		int err = errno;

		close(client_sd);
		client_sd = -1;

		errno = err;
		return -1;
	}

	return 0;
}

/**
 * ipc_server_read - Read IPC message from client
 * @buf: Buffer for message
 * @len: Size of @buf in bytes
 *
 * Reads a message from the IPC socket and stores in @buf, respecting
 * the size @len.  Connects and resets connection as necessary.
 *
 * Returns:
 * Pointer to a successfuly read command packet in @buf, or %NULL on error.
 */
struct cmd *ipc_server_read(uint8 buf[], int len)
{
	size_t size;
	socklen_t socklen = 0;

	/* sanity check */
	if (server_sd < 0) {
		errno = EBADF;
		return NULL;
	}

	/* wait for connections */
	if (client_sd < 0) {
		client_sd = accept(server_sd, NULL, &socklen);
		if (client_sd < 0)
			return NULL;
	}

	size = recv(client_sd, buf, len, 0);
	if (!size) {
		close(client_sd);
		client_sd = -1;
		errno = ECONNRESET;
		return NULL;
	}

	/* successful read */
	if (size >= sizeof(struct cmd)) {
		struct cmd *p = (struct cmd *)buf;

		if (size == p->len)
			return p;
	}

	errno = EAGAIN;
	return NULL;
}

/**
 * ipc_send - Send message to peer
 * @buf: Message to send
 * @len: Message length in bytes of @buf
 *
 * Sends the IPC message in @buf of the size @len to the peer.
 *
 * Returns:
 * Number of bytes successfully sent, or -1 with @errno on failure.
 */
int ipc_send(const void *buf, int len)
{
	/* sanity check */
	if (client_sd < 0) {
		errno = EBADF;
		return -1;
	}

	if (write(client_sd, buf, len) != len)
		return -1;

	return len;
}

/**
 * ipc_receive - Receive message from peer
 * @buf: Buffer to receive message in
 * @len: Buffer size in bytes
 *
 * Waits to receive an IPC message in @buf of max @len bytes from the peer.
 *
 * Returns:
 * Number of bytes successfully received, or -1 with @errno on failure.
 */
int ipc_receive(uint8 buf[], int len)
{
	/* sanity check */
	if (client_sd < 0) {
		errno = EBADF;
		return -1;
	}

	return read(client_sd, buf, len);
}

/**
 * ipc_exit - Tear down and cleanup IPC communication.
 */
void ipc_exit(void)
{
	if (server_sd >= 0) {
		close(server_sd);
		unlink(SOCKET_PATH);
	}

	if (client_sd >= 0)
		close(client_sd);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
