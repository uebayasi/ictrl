/*	$OpenBSD: util.c,v 1.7 2016/03/20 00:01:22 krw Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
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

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buf.h"
#include "ictrl.h"

struct pdu *
pdu_new(void)
{
	struct pdu *p;

	if (!(p = calloc(1, sizeof(*p))))
		return NULL;
	return p;
}

void *
pdu_alloc(size_t len)
{
	return malloc(PDU_LEN(len));
}

void *
pdu_dup(void *data, size_t len)
{
	void *p;

	if ((p = malloc(PDU_LEN(len))))
		memcpy(p, data, len);
	return p;
}

int
pdu_addbuf(struct pdu *p, void *buf, size_t len, unsigned int elm)
{
	if (len & 0x3) {
		bzero((char *)buf + len, 4 - (len & 0x3));
		len += 4 - (len & 0x3);
	}

	if (elm < PDU_MAXIOV)
		if (!p->iov[elm].iov_base) {
			p->iov[elm].iov_base = buf;
			p->iov[elm].iov_len = len;
			return 0;
		}

	/* no space left */
	return -1;
}

void *
pdu_getbuf(struct pdu *p, size_t *len, unsigned int elm)
{
	if (len)
		*len = 0;
	if (elm < PDU_MAXIOV)
		if (p->iov[elm].iov_base) {
			if (len)
				*len = p->iov[elm].iov_len;
			return p->iov[elm].iov_base;
		}

	return NULL;
}

void
pdu_free(struct pdu *p)
{
	unsigned int j;

	for (j = 0; j < PDU_MAXIOV; j++)
		free(p->iov[j].iov_base);
	free(p);
}

void
pdu_free_queue(struct pduq *channel)
{
	struct pdu *p;

	while ((p = TAILQ_FIRST(channel))) {
		TAILQ_REMOVE(channel, p, entry);
		pdu_free(p);
	}
}

struct pdu *
pdu_get(char *buf, size_t len)
{
	struct pdu *p;
	struct ctrlmsghdr *cmh;
	void *data;
	size_t n;
	int i;

	if (len < sizeof(*cmh))
		return NULL;

	if (!(p = pdu_new()))
		return NULL;

	n = sizeof(*cmh);
	cmh = pdu_alloc(n);
	memcpy(cmh, buf, n);
	buf += n;
	len -= n;

	if (pdu_addbuf(p, cmh, n, 0)) {
		free(cmh);
fail:
		pdu_free(p);
		return NULL;
	}

	for (i = 0; i < 3; i++) {
		n = cmh->len[i];
		if (n == 0)
			continue;
		if (PDU_LEN(n) > len)
			goto fail;
		if (!(data = pdu_alloc(n)))
			goto fail;
		memcpy(data, buf, n);
		if (pdu_addbuf(p, data, n, i + 1)) {
			free(data);
			goto fail;
		}
		buf += PDU_LEN(n);
		len -= PDU_LEN(n);
	}

	return p;
}
