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

#define PDU_READ_SIZE		(256 * 1024)
#define CONTROL_READ_SIZE	8192
#define PDU_MAXIOV		5
#define PDU_WRIOV		(PDU_MAXIOV * 8)
#define PDU_ALIGN		4
#define PDU_MASK		(PDU_ALIGN - 1)
#define PDU_LEN(x)		((((x) + PDU_MASK) / PDU_ALIGN) * PDU_ALIGN)

struct pdu {
	TAILQ_ENTRY(pdu)	 entry;
	struct iovec		 iov[PDU_MAXIOV];
	size_t			 resid;
};
TAILQ_HEAD(pduq, pdu);

struct pdu *pdu_new(void);
void	*pdu_alloc(size_t);
void	*pdu_dup(void *, size_t);
int	pdu_addbuf(struct pdu *, void *, size_t, unsigned int);
void	*pdu_getbuf(struct pdu *, size_t *, unsigned int);
void	pdu_free(struct pdu *);
void	pdu_free_queue(struct pduq *);

#endif /* _ICTRL_BUF_H_ */
