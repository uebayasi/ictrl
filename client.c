/*	$OpenBSD: iscsictl.c,v 1.10 2015/11/11 02:55:12 deraadt Exp $ */

/*
 * Copyright (c) 2010 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>	/* nitems */
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <event.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "ictrl.h"
#include "buf.h"
#include "client.h"

struct client_state *
ictrl_connect(struct client_config *cf)
{
	struct client_state *client;
	struct sockaddr_un sun;

	if ((client = calloc(1, sizeof(*client))) == NULL) {
		log_warn("ictrl_init: calloc");
		return NULL;
	}

	TAILQ_INIT(&client->channel);
	if ((client->fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, cf->sockname, sizeof(sun.sun_path));

	if (connect(client->fd, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", cf->sockname);

	return client;
}

void
ictrl_close(struct client_state *client)
{
	close(client->fd);
}

void
ictrl_queue(void *ch, struct pdu *pdu)
{
	struct client_state *client = ch;

	TAILQ_INSERT_TAIL(&client->channel, pdu, entry);
}

int
ctl_sendpdu(int fd, struct pdu *pdu)
{
	struct iovec iov[PDU_MAXIOV];
	struct msghdr msg;
	unsigned int niov = 0;

	for (niov = 0; niov < PDU_MAXIOV; niov++) {
		iov[niov].iov_base = pdu->iov[niov].iov_base;
		iov[niov].iov_len = pdu->iov[niov].iov_len;
	}
	bzero(&msg, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = niov;
	if (sendmsg(fd, &msg, 0) == -1)
		return -1;
	return 0;
}

struct pdu *
ctl_recvpdu(int fd, char *buf, size_t buflen)
{
	struct ctrlmsghdr *cmh;
	ssize_t n;

	if ((n = recv(fd, buf, buflen, 0)) == -1 &&
	    !(errno == EAGAIN || errno == EINTR))
		err(1, "recv");

	if (n == 0)
		errx(1, "connection to iscsid closed");

	return pdu_get(buf, n);
}
