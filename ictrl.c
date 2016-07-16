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
static struct pdu *
		ictrl_getpdu(char *, size_t);

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

	/* set socket non-blocking */
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
		struct pdu *pdu;

		if ((pdu = ictrl_recv(c)) == NULL) {
			ictrl_server_close(c);
			return;
		}
		(*c->state->config->proc)(c, pdu);
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
	struct pdu *pdu;

	event_del(&c->ev);
	close(c->fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&c->state->evt, NULL)) {
		evtimer_del(&c->state->evt);
		event_add(&c->state->ev, NULL);
	}

	while ((pdu = TAILQ_FIRST(&c->channel))) {
		TAILQ_REMOVE(&c->channel, pdu, entry);
		pdu_free(pdu);
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
	return ictrl_compose(c, type, 1, CTRLARGV({ buf, len }));
}

int
ictrl_compose(struct ictrl_session *c, u_int16_t type, int argc,
    struct iovec *argv)
{
	struct pdu *pdu;
	struct ictrl_msghdr *cmh;
	size_t size = 0;
	int i;

	if (argc > (int)nitems(cmh->len))
		return -1;

	for (i = 0; i < argc; i++)
		size += argv[i].iov_len;
	if (PDU_LEN(size) > sizeof(c->buf) - PDU_LEN(sizeof(*cmh)))
		return -1;

	if ((pdu = pdu_new()) == NULL)
		return -1;
	if ((cmh = malloc(sizeof(*cmh))) == NULL)
		goto fail;
	bzero(cmh, sizeof(*cmh));
	cmh->type = type;
	pdu_addbuf(pdu, cmh, sizeof(*cmh), 0);

	for (i = 0; i < argc; i++)
		if (argv[i].iov_len > 0) {
			void *ptr;

			cmh->len[i] = argv[i].iov_len;
			if ((ptr = pdu_alloc(argv[i].iov_len)) == NULL)
				goto fail;
			memcpy(ptr, argv[i].iov_base, argv[i].iov_len);
			pdu_addbuf(pdu, ptr, argv[i].iov_len, i + 1);
		}

	TAILQ_INSERT_TAIL(&c->channel, pdu, entry);

	/*
	 * Schedule a next event for server.
	 */
	if (c->fd != -1)
		ictrl_server_trigger(c);

	return 0;
fail:
	pdu_free(pdu);
	return -1;
}

int
ictrl_send(struct ictrl_session *c)
{
	struct msghdr msg;
	struct pdu *pdu;
	struct iovec iov[nitems(pdu->iov)];
	unsigned int niov = 0;
	int fd = (c->fd != -1) ? c->fd : c->state->fd;

	if ((pdu = TAILQ_FIRST(&c->channel)) != NULL) {

		for (niov = 0; niov < nitems(iov); niov++) {
			iov[niov].iov_base = pdu->iov[niov].iov_base;
			iov[niov].iov_len = pdu->iov[niov].iov_len;
		}
		bzero(&msg, sizeof(msg));
		msg.msg_iov = iov;
		msg.msg_iovlen = niov;
		if (sendmsg(fd, &msg, 0) == -1) {
			if (errno == EAGAIN || errno == ENOBUFS)
				return EAGAIN;
			return -1;
		}
		TAILQ_REMOVE(&c->channel, pdu, entry);
	}
	return 0;
}

struct pdu *
ictrl_recv(struct ictrl_session *c)
{
	ssize_t n;
	int fd = (c->fd != -1) ? c->fd : c->state->fd;

	if ((n = recv(fd, c->buf, sizeof(c->buf), 0)) == -1 &&
	    !(errno == EAGAIN || errno == EINTR))
		return NULL;

	if (n == 0)
		return NULL;

	return ictrl_getpdu(c->buf, n);
}

static struct pdu *
ictrl_getpdu(char *buf, size_t len)
{
	struct pdu *p;
	struct ictrl_msghdr *cmh;
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

	for (i = 0; i < nitems(cmh->len); i++) {
		n = cmh->len[i];
		if (n == 0)
			continue;
		if (PDU_LEN(n) > len)
			goto fail;
		if ((data = pdu_alloc(n)) == NULL)
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
