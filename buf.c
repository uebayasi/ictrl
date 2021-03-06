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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buf.h"

struct cbuf *
cbuf_new(void)
{
	struct cbuf *cbuf;

	if ((cbuf = calloc(1, sizeof(*cbuf))) == NULL)
		return NULL;
	return cbuf;
}

void *
cbuf_alloc(size_t len)
{
	return malloc(CBUF_LEN(len));
}

void *
cbuf_dup(void *data, size_t len)
{
	void *cbuf;

	if ((cbuf = malloc(CBUF_LEN(len))))
		memcpy(cbuf, data, len);
	return cbuf;
}

int
cbuf_addbuf(struct cbuf *cbuf, void *buf, size_t len)
{
	if (len & CBUF_MASK) {
		bzero((char *)buf + len, CBUF_ALIGN - (len & CBUF_MASK));
		len = CBUF_LEN(len);
	}
	if (cbuf->iovlen >= nitems(cbuf->iov))
		return -1;
	cbuf->iov[cbuf->iovlen].iov_base = buf;
	cbuf->iov[cbuf->iovlen].iov_len = len;
	cbuf->iovlen++;
	return 0;
}

void *
cbuf_getbuf(struct cbuf *cbuf, size_t *len, unsigned int elm)
{
	if (len != NULL)
		*len = 0;
	if (elm >= nitems(cbuf->iov))
		return NULL;
	if (cbuf->iov[elm].iov_base == 0)
		return NULL;
	if (len != NULL)
		*len = cbuf->iov[elm].iov_len;
	return cbuf->iov[elm].iov_base;
}

void
cbuf_free(struct cbuf *cbuf)
{
	unsigned int i;

	for (i = 0; i < nitems(cbuf->iov); i++)
		free(cbuf->iov[i].iov_base);
	free(cbuf);
}

struct cbuf *
cbuf_compose(int argc, struct iovec *argv)
{
	struct cbuf *cbuf;
	struct cbuf_msghdr *cmh;
	size_t n = 0;
	int i;

	if (argc > (int)nitems(cmh->len))
		return NULL;

	for (i = 0; i < argc; i++)
		n += argv[i].iov_len;
	if (CBUF_LEN(n) > CBUF_BUF_SIZE - CBUF_LEN(sizeof(*cmh)))
		return NULL;

	if ((cbuf = cbuf_new()) == NULL)
		return NULL;
	if ((cmh = malloc(sizeof(*cmh))) == NULL)
		goto fail;
	bzero(cmh, sizeof(*cmh));
	cbuf_addbuf(cbuf, cmh, sizeof(*cmh));

	for (i = 0; i < argc; i++) {
		void *ptr;

		if (argv[i].iov_len <= 0)
			continue;
		cmh->len[i] = argv[i].iov_len;
		if ((ptr = cbuf_alloc(argv[i].iov_len)) == NULL)
			goto fail;
		memcpy(ptr, argv[i].iov_base, argv[i].iov_len);
		cbuf_addbuf(cbuf, ptr, argv[i].iov_len);
	}

	return cbuf;

fail:
	cbuf_free(cbuf);
	return NULL;
}

struct cbuf *
cbuf_decompose(char *buf, size_t len)
{
	struct cbuf *cbuf;
	struct cbuf_msghdr *cmh;
	size_t n;
	int i;

	if (len < sizeof(*cmh))
		return NULL;

	if ((cbuf = cbuf_new()) == NULL)
		return NULL;

	n = sizeof(*cmh);
	cmh = cbuf_alloc(n);
	memcpy(cmh, buf, n);
	buf += n;
	len -= n;

	if (cbuf_addbuf(cbuf, cmh, n)) {
		free(cmh);
		goto fail;
	}

	for (i = 0; i < nitems(cmh->len); i++) {
		void *ptr;

		n = cmh->len[i];
		if (n == 0)
			continue;
		if (CBUF_LEN(n) > len)
			goto fail;
		if ((ptr = cbuf_alloc(n)) == NULL)
			goto fail;
		memcpy(ptr, buf, n);
		if (cbuf_addbuf(cbuf, ptr, n)) {
			free(ptr);
			goto fail;
		}
		buf += CBUF_LEN(n);
		len -= CBUF_LEN(n);
	}

	return cbuf;

fail:
	cbuf_free(cbuf);
	return NULL;
}
