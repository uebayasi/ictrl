/*	$OpenBSD: iscsid.h,v 1.14 2014/05/10 11:30:47 claudio Exp $ */

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

#ifndef _ICTRL_BUF_H_
#define _ICTRL_BUF_H_

#include <sys/queue.h>
#include <sys/uio.h>

#define CBUF_MAXIOV		5
#define CBUF_WRIOV		(CBUF_MAXIOV * 8)
#define CBUF_ALIGN		4
#define CBUF_MASK		(CBUF_ALIGN - 1)
#define CBUF_LEN(x)		((((x) + CBUF_MASK) / CBUF_ALIGN) * CBUF_ALIGN)

struct cbuf {
	TAILQ_ENTRY(cbuf)	 entry;
	struct iovec		 iov[CBUF_MAXIOV];
	unsigned int		 iovlen;
};
TAILQ_HEAD(cbufq, cbuf);

struct cbuf *cbuf_new(void);
void	*cbuf_alloc(size_t);
void	*cbuf_dup(void *, size_t);
int	cbuf_addbuf(struct cbuf *, void *, size_t);
void	*cbuf_getbuf(struct cbuf *, size_t *, unsigned int);
void	cbuf_free(struct cbuf *);

#endif /* _ICTRL_BUF_H_ */
