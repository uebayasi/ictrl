/*	$OpenBSD: control.c,v 1.9 2016/04/05 00:52:35 yasuoka Exp $ */

/*
 * Copyright (c) 2016 Masao Uebayashi <uebayasi@tombiinc.com>
 * Copyright (c) 2010 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <sys/param.h> /* nitems */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "buf.h"
#include "ictrl.h"

static void	ictrl_server_accept(int, short, void *);
static void	ictrl_server_dispatch(int, short, void *);
static void	ictrl_server_close(struct ictrl_session *);
static void	ictrl_server_trigger(struct ictrl_session *);
static struct cbuf *
		ictrl_compose(int, struct iovec *);
static struct cbuf *
		ictrl_decompose(char *, size_t);

/*
 * API for server
 */

struct ictrl_state *
ictrl_server_init(struct ictrl_config *cf)
{
	struct ictrl_state	*ctrl;
	struct sockaddr_un	 sun;
	int			 fd;
	int			 flags;
	mode_t			 old_umask;

	if ((ctrl = calloc(1, sizeof(*ctrl))) == NULL) {
		log_warn("%s: calloc", __func__);
		return NULL;
	}

	if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
		log_warn("%s: socket", __func__);
		return NULL;
	}

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, cf->path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path)) {
		log_warnx("%s: path %s too long", __func__, cf->path);
		return NULL;
	}

	if (unlink(cf->path) == -1)
		if (errno != ENOENT) {
			log_warn("%s: unlink %s", __func__, cf->path);
			close(fd);
			return NULL;
		}

	old_umask = umask(S_IXUSR | S_IXGRP | S_IWOTH | S_IROTH | S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("%s: bind: %s", __func__, cf->path);
		close(fd);
		umask(old_umask);
		return NULL;
	}
	umask(old_umask);

	if (chmod(cf->path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) == -1) {
		log_warn("%s: chmod", __func__);
		close(fd);
		(void)unlink(cf->path);
		return NULL;
	}

	if (listen(fd, cf->backlog) == -1) {
		log_warn("%s: listen", __func__);
		close(fd);
		(void)unlink(cf->path);
		return NULL;
	}

	/* Set socket non-blocking. */
	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return NULL;
	flags |= O_NONBLOCK;
	if ((flags = fcntl(fd, F_SETFL, flags)) == -1)
		return NULL;

	ctrl->config = cf;
	ctrl->fd = fd;

	return ctrl;
}

void
ictrl_server_fini(struct ictrl_state *ctrl)
{
	if (ctrl->config->path)
		unlink(ctrl->config->path);
	close(ctrl->fd);
	free(ctrl);
}

void
ictrl_server_start(struct ictrl_state *ctrl)
{
	event_set(&ctrl->ev, ctrl->fd, EV_READ, ictrl_server_accept, ctrl);
	event_add(&ctrl->ev, NULL);
	evtimer_set(&ctrl->evt, ictrl_server_accept, ctrl);
}

void
ictrl_server_stop(struct ictrl_state *ctrl)
{
	event_del(&ctrl->ev);
	event_del(&ctrl->evt);
}

static void
ictrl_server_accept(int listenfd, short event, void *v)
{
	struct ictrl_state	*ctrl = v;
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 sun;
	struct ictrl_session	*c;

	event_add(&ctrl->ev, NULL);
	if ((event & EV_TIMEOUT))
		return;

	len = sizeof(sun);
	if ((connfd = accept(listenfd,
	    (struct sockaddr *)&sun, &len)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&ctrl->ev);
			evtimer_add(&ctrl->evt, &evtpause);
		} else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("%s", __func__);
		return;
	}

	if ((c = malloc(sizeof(struct ictrl_session))) == NULL) {
		log_warn("%s", __func__);
		close(connfd);
		return;
	}

	TAILQ_INIT(&c->channel);
	c->state = ctrl;
	c->fd = connfd;
	ictrl_server_trigger(c);
}

static void
ictrl_server_dispatch(int fd, short event, void *v)
{
	struct ictrl_session *c = v;

	if (event & EV_TIMEOUT) {
		log_debug("%s: control connection (fd %d) timed out.",
		    __func__, fd);
		ictrl_server_close(c);
		return;
	}
	if (event & EV_READ) {
		struct cbuf *cbuf;

		if ((cbuf = ictrl_recv(c)) == NULL) {
			ictrl_server_close(c);
			return;
		}
		(*c->state->config->proc)(c, cbuf);
	}
	if (event & EV_WRITE) {
		switch (ictrl_send(c)) {
		case -1:
			ictrl_server_close(c);
			return;
		case EAGAIN:
		default:
			break;
		}
	}
	ictrl_server_trigger(c);
}

static void
ictrl_server_close(struct ictrl_session *c)
{
	struct cbuf *cbuf;

	event_del(&c->ev);
	close(c->fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&c->state->evt, NULL)) {
		evtimer_del(&c->state->evt);
		event_add(&c->state->ev, NULL);
	}

	while ((cbuf = TAILQ_FIRST(&c->channel))) {
		TAILQ_REMOVE(&c->channel, cbuf, entry);
		cbuf_free(cbuf);
	}
	free(c);
}

static void
ictrl_server_trigger(struct ictrl_session *c)
{
	short flags = EV_READ | (TAILQ_EMPTY(&c->channel) ? 0 : EV_WRITE);

	event_del(&c->ev);
	event_set(&c->ev, c->fd, flags, ictrl_server_dispatch, c);
	event_add(&c->ev, NULL);
}

/*
 * API for client
 */

struct ictrl_session *
ictrl_client_init(struct ictrl_config *cf)
{
	struct ictrl_state	*ctrl;
	struct sockaddr_un	 sun;
	int			 fd;
	struct ictrl_session	*c;

	if ((ctrl = calloc(1, sizeof(*ctrl))) == NULL) {
		log_warn("%s: calloc", __func__);
		return NULL;
	}

	if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, cf->path, sizeof(sun.sun_path));

	if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", cf->path);

	ctrl->config = cf;
	ctrl->fd = fd;

	if ((c = malloc(sizeof(struct ictrl_session))) == NULL) {
		close(ctrl->fd);
		free(ctrl);
		return NULL;
	}

	c->state = ctrl;
	TAILQ_INIT(&c->channel);
	c->fd = -1;

	return c;
}

void
ictrl_client_fini(struct ictrl_session *c)
{
	struct ictrl_state	*ctrl = c->state;

	free(c);
	close(ctrl->fd);
	free(ctrl);
}

/*
 * API for both server and client
 */

#define CTRLARGV(x...)	((struct iovec []){ x })

int
ictrl_build(struct ictrl_session *c, u_int16_t type, void *buf, size_t len)
{
	return ictrl_buildv(c, type, 1, CTRLARGV({ buf, len }));
}

int
ictrl_buildv(struct ictrl_session *c, u_int16_t type, int argc,
    struct iovec *argv)
{
	struct cbuf *cbuf;
	struct cbuf_msghdr *cmh;

	cbuf = ictrl_compose(argc, argv);
	if (cbuf == NULL)
		return -1;
	cmh = cbuf_getbuf(cbuf, NULL, 0);
	cmh->type = type;

	TAILQ_INSERT_TAIL(&c->channel, cbuf, entry);

	/*
	 * Schedule a next event for server.
	 */
	if (c->fd != -1)
		ictrl_server_trigger(c);

	return 0;
}

static struct cbuf *
ictrl_compose(int argc, struct iovec *argv)
{
	struct cbuf *cbuf;
	struct cbuf_msghdr *cmh;
	size_t n = 0;
	int i;

	if (argc > (int)nitems(cmh->len))
		return NULL;

	for (i = 0; i < argc; i++)
		n += argv[i].iov_len;
	if (CBUF_LEN(n) > ICTRL_READ_SIZE - CBUF_LEN(sizeof(*cmh)))
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

static struct cbuf *
ictrl_decompose(char *buf, size_t len)
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

int
ictrl_send(struct ictrl_session *c)
{
	struct cbuf *cbuf;

	if ((cbuf = TAILQ_FIRST(&c->channel)) != NULL) {
		struct msghdr msg;
		int fd = (c->fd != -1) ? c->fd : c->state->fd;

		bzero(&msg, sizeof(msg));
		msg.msg_iov = cbuf->iov;
		msg.msg_iovlen = cbuf->iovlen;
		if (sendmsg(fd, &msg, 0) == -1) {
			if (errno == EAGAIN || errno == ENOBUFS)
				return EAGAIN;
			return -1;
		}
		TAILQ_REMOVE(&c->channel, cbuf, entry);
	}
	return 0;
}

struct cbuf *
ictrl_recv(struct ictrl_session *c)
{
	ssize_t n;
	int fd = (c->fd != -1) ? c->fd : c->state->fd;

	if ((n = recv(fd, c->buf, sizeof(c->buf), 0)) == -1 &&
	    !(errno == EAGAIN || errno == EINTR))
		return NULL;

	if (n == 0)
		return NULL;

	return ictrl_decompose(c->buf, n);
}
