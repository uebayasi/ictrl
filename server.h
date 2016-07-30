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

#include <event.h>

struct server_config;
struct server_ops;
struct server_context;

struct server_config {
	char *username;
	int exit_wait;
	int verbose;
	int debug;
	int nobkill;
	struct server_ops *ops;
};

struct server_ops {
	void		(*init)(void *);
	void		(*fini)(void *);
	void		(*start)(void *);
	void		(*stop)(void *);
	void		(*shutdown)(void *);
	int		(*isdown)(void *);
};

struct server_context {
	struct server_config *
			config;
	void		*data;
	struct event	exit_ev;
	int		exit_rounds;
};

struct server_context *
		server_init(struct server_config *, void *);
void		server_fini(struct server_context *);
void		server_loop(struct server_context *);
