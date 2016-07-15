/*	$OpenBSD: iscsid.c,v 1.18 2015/01/16 15:57:06 deraadt Exp $ */

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
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <err.h>
#include <event.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "server.h"

void		server_check(struct server_context *);
void		server_drop(struct server_context *);
void		server_sigdisp(int, short, void *);
void		server_shutdown_cb(int, short, void *);

struct server_context *
server_init(struct server_config *cf, void *data)
{
	struct server_context *ctx;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return NULL;
	ctx->config = cf;
	ctx->data = data;

	server_check(ctx);
	(*ctx->config->ops->init)(ctx);
	server_drop(ctx);

	return ctx;
}

void
server_check(struct server_context *ctx)
{
	log_init(ctx->config->debug);
	log_verbose(ctx->config->verbose);

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");
}

void
server_drop(struct server_context *ctx)
{
	struct passwd *pw;

	if (!ctx->config->debug)
		daemon(1, 0);

	log_info("startup");

	int name[] = { CTL_KERN, KERN_PROC_NOBROADCASTKILL, 0 };
	name[2] = getpid();
	if (sysctl(name, 3, NULL, 0, &ctx->config->nobkill,
	    sizeof(ctx->config->nobkill)) != 0)
		fatal("sysctl");

	if ((pw = getpwnam(ctx->config->username)) == NULL)
		errx(1, "unknown user %s", ctx->config->username);

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");
}

void
server_loop(struct server_context *ctx)
{
	struct event ev_sigint, ev_sigterm, ev_sighup;

	event_init();
	signal_set(&ev_sigint, SIGINT, server_sigdisp, ctx);
	signal_set(&ev_sigterm, SIGTERM, server_sigdisp, ctx);
	signal_set(&ev_sighup, SIGHUP, server_sigdisp, ctx);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	(*ctx->config->ops->start)(ctx);
	event_dispatch();
	(*ctx->config->ops->stop)(ctx);

	log_info("exiting");
}

void
server_shutdown_cb(int fd, short event, void *arg)
{
	struct server_context *ctx = arg;
	struct timeval tv;

	if (ctx->exit_rounds++ >= ctx->config->exit_wait ||
	    (*ctx->config->ops->isdown)(ctx))
		event_loopexit(NULL);

	timerclear(&tv);
	tv.tv_sec = 1;

	if (evtimer_add(&ctx->exit_ev, &tv) == -1)
		fatal("%s", __func__);
}

void
server_sigdisp(int sig, short event, void *arg)
{
	struct server_context *ctx = arg;
	struct timeval tv;

	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGINT:
	case SIGTERM:
	case SIGHUP:
		(*ctx->config->ops->shutdown)(ctx);
		evtimer_set(&ctx->exit_ev, server_shutdown_cb, ctx);
		timerclear(&tv);
		if (evtimer_add(&ctx->exit_ev, &tv) == -1)
			fatal("%s", __func__);
		break;
	default:
		fatalx("unexpected signal");
		/* NOTREACHED */
	}
}
