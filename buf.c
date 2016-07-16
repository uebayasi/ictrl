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
	if (len & PDU_MASK) {
		bzero((char *)buf + len, PDU_ALIGN - (len & PDU_MASK));
		len = PDU_LEN(len);
	}

	if (elm < nitems(p->iov))
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
	if (elm < nitems(p->iov))
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

	for (j = 0; j < nitems(p->iov); j++)
		free(p->iov[j].iov_base);
	free(p);
}
