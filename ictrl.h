/*
 * Copyright (c) 2016 Masao Uebayashi <uebayasi@tombiinc.com>
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

#ifndef _ICTRL_ICTRL_H_
#define _ICTRL_ICTRL_H_

#include <sys/queue.h>

#include <event.h>

#include "buf.h"

#define ICTRL_READ_SIZE		8192
#define ICTRL_BUF_NUM		(CBUF_MAXIOV - 1/* cmh */)

struct ictrl_config;
struct ictrl_session;
struct ictrl_state;
struct ictrl_msghdr;

struct ictrl_config {
	char			*path;
	int			backlog;
	void			(*proc)(struct ictrl_session *,
				    struct cbuf *);
};

struct ictrl_session {
	struct ictrl_state	*state;
	struct cbufq		channel;
	char			buf[ICTRL_READ_SIZE];
	int			fd;	/* accept fd; only for server */
	struct event		ev;	/* dispatch; only for server */
};

struct ictrl_state {
	struct ictrl_config	*config;
	int			fd;	/* socket fd */
	struct event		ev;	/* accept; only for server */
	struct event		evt;	/* accept; only for server */
};

struct ictrl_state *
		ictrl_server_init(struct ictrl_config *);
void		ictrl_server_fini(struct ictrl_state *);
void		ictrl_server_start(struct ictrl_state *);
void		ictrl_server_stop(struct ictrl_state *);

struct ictrl_session *
		ictrl_client_init(struct ictrl_config *);
void		ictrl_client_fini(struct ictrl_session *);

int		ictrl_build(struct ictrl_session *, u_int16_t, void *,
		    size_t);
int		ictrl_buildv(struct ictrl_session *, u_int16_t, int,
		    struct iovec *);
int		ictrl_send(struct ictrl_session *);
struct cbuf	*ictrl_recv(struct ictrl_session *);

/*
 * Common control message header.
 * A message can consist of up to 3 parts with specified length.
 */

struct ictrl_msghdr {
	u_int16_t	type;
	u_int16_t	len[ICTRL_BUF_NUM];
};

#endif /* _ICTRL_ICTRL_H_ */
