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

#define	CONTROL_BACKLOG	5

static void	ictrl_accept(int, short, void *);
static void	ictrl_dispatch(int, short, void *);
static void	ictrl_close(struct ictrl_session *);
static void	ictrl_schedule(struct ictrl_session *);
static int	ictrl_send(int, struct ictrl_session *);
static struct pdu
		*ictrl_recv(int, struct ictrl_session *);

struct ictrl_state *
ictrl_init(struct ictrl_config *cf)
{
	struct ictrl_state	*ctrl;
	struct sockaddr_un	 sun;
	int			 fd;
	int			 flags;
	mode_t			 old_umask;

	if ((ctrl = calloc(1, sizeof(*ctrl))) == NULL) {
		log_warn("ictrl_init: calloc");
		return NULL;
	}

	if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
		log_warn("ictrl_init: socket");
		return NULL;
	}

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, cf->path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path)) {
		log_warnx("ictrl_init: path %s too long", cf->path);
		return NULL;
	}

	if (unlink(cf->path) == -1)
		if (errno != ENOENT) {
			log_warn("ictrl_init: unlink %s", cf->path);
			close(fd);
			return NULL;
		}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("ictrl_init: bind: %s", cf->path);
		close(fd);
		umask(old_umask);
		return NULL;
	}
	umask(old_umask);

	if (chmod(cf->path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		log_warn("ictrl_init: chmod");
		close(fd);
		(void)unlink(cf->path);
		return NULL;
	}

	if (listen(fd, CONTROL_BACKLOG) == -1) {
		log_warn("ictrl_init: listen");
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

// XXX before event_dispatch()
void
ictrl_event_init(struct ictrl_state *ctrl)
{
	event_set(&ctrl->ev, ctrl->fd, EV_READ, ictrl_accept, ctrl);
	event_add(&ctrl->ev, NULL);
	evtimer_set(&ctrl->evt, ictrl_accept, ctrl);
}

// XXX after event_dispatch()
void
ictrl_cleanup(struct ictrl_state *ctrl)
{
	event_del(&ctrl->ev);
	event_del(&ctrl->evt);
	close(ctrl->fd);
	free(ctrl);

	/* XXX fini */
	if (ctrl->config->path)
		unlink(ctrl->config->path);
}

/* ARGSUSED */
static void
ictrl_accept(int listenfd, short event, void *v)
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
			log_warn("ictrl_accept");
		return;
	}

	if ((c = malloc(sizeof(struct ictrl_session))) == NULL) {
		log_warn("ictrl_accept");
		close(connfd);
		return;
	}

	TAILQ_INIT(&c->channel);
	c->state = ctrl;
	c->fd = connfd;
	ictrl_schedule(c);
}

/* ARGSUSED */
static void
ictrl_dispatch(int fd, short event, void *v)
{
	struct ictrl_session *c = v;
	struct pdu *pdu;
	short flags = EV_READ;

	if (event & EV_TIMEOUT) {
		log_debug("control connection (fd %d) timed out.", fd);
		ictrl_close(c);
		return;
	}
	if (event & EV_READ) {
		if ((pdu = ictrl_recv(fd, c)) == NULL) {
			ictrl_close(c);
			return;
		}
		(*c->state->config->proc)(c, pdu);
	}
	if (event & EV_WRITE) {
		switch (ictrl_send(fd, c)) {
		case -1:
			ictrl_close(c);
			return;
		case EAGAIN:
		default:
			break;
		}
	}
	ictrl_schedule(c);
}

static void
ictrl_close(struct ictrl_session *c)
{
	event_del(&c->ev);
	close(c->fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&c->state->evt, NULL)) {
		evtimer_del(&c->state->evt);
		event_add(&c->state->ev, NULL);
	}

	pdu_free_queue(&c->channel);
	free(c);
}

int
ictrl_compose(void *ch, u_int16_t type, void *buf, size_t len)
{
	return ictrl_build(ch, type, 1, CTRLARGV({ buf, len }));
}

int
ictrl_build(void *ch, u_int16_t type, int argc, struct ctrldata *argv)
{
	struct ictrl_session *c = ch;
	struct pdu *pdu;
	struct ctrlmsghdr *cmh;
	size_t size = 0;
	int i;

	if (argc > (int)nitems(cmh->len))
		return -1;

	for (i = 0; i < argc; i++)
		size += argv[i].len;
	if (PDU_LEN(size) > CONTROL_READ_SIZE - PDU_LEN(sizeof(*cmh)))
		return -1;

	if ((pdu = pdu_new()) == NULL)
		return -1;
	if ((cmh = malloc(sizeof(*cmh))) == NULL)
		goto fail;
	bzero(cmh, sizeof(*cmh));
	cmh->type = type;
	pdu_addbuf(pdu, cmh, sizeof(*cmh), 0);

	for (i = 0; i < argc; i++)
		if (argv[i].len > 0) {
			void *ptr;

			cmh->len[i] = argv[i].len;
			if ((ptr = pdu_alloc(argv[i].len)) == NULL)
				goto fail;
			memcpy(ptr, argv[i].buf, argv[i].len);
			pdu_addbuf(pdu, ptr, argv[i].len, i + 1);
		}

	TAILQ_INSERT_TAIL(&c->channel, pdu, entry);
	ictrl_schedule(c);
	return 0;
fail:
	pdu_free(pdu);
	return -1;
}

static void
ictrl_schedule(struct ictrl_session *c)
{
	short flags = EV_READ;

	if (!TAILQ_EMPTY(&c->channel))
		flags |= EV_WRITE;
	event_del(&c->ev);
	event_set(&c->ev, c->fd, flags, ictrl_dispatch, c);
	event_add(&c->ev, NULL);
}

static int
ictrl_send(int fd, struct ictrl_session *c)
{
	struct iovec iov[PDU_MAXIOV];
	struct msghdr msg;
	struct pdu *pdu;
	unsigned int niov = 0;

	if ((pdu = TAILQ_FIRST(&c->channel)) != NULL) {
		for (niov = 0; niov < PDU_MAXIOV; niov++) {
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

static struct pdu *
ictrl_recv(int fd, struct ictrl_session *c)
{
	ssize_t n;

	if ((n = recv(fd, c->buf, sizeof(c->buf), 0)) == -1 &&
	    !(errno == EAGAIN || errno == EINTR))
		return NULL;

	if (n == 0)
		return NULL;

	return pdu_get(c->buf, n);
}
