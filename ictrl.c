/*	$OpenBSD: control.c,v 1.9 2016/04/05 00:52:35 yasuoka Exp $ */

/*
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

struct ictrl_session {
	struct event		ev;
	//struct pduq		channel;
	int			fd;
};

struct ictrl_state {
	struct event		ev;
	struct event		evt;
	int			fd;
	void			(*dispatch)(int, short, void *);
};

#define	CONTROL_BACKLOG	5

void	ictrl_accept(int, short, void *);
void	ictrl_close(struct ictrl_state *, struct ictrl_session *);

struct ictrl_state *
ictrl_init(char *path, void (*dispatch)(int, short, void *))
{
	struct ictrl_state	*ctrl;
	struct sockaddr_un	 sun;
	int			 fd;
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
	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path)) {
		log_warnx("ictrl_init: path %s too long", path);
		return NULL;
	}

	if (unlink(path) == -1)
		if (errno != ENOENT) {
			log_warn("ictrl_init: unlink %s", path);
			close(fd);
			return NULL;
		}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("ictrl_init: bind: %s", path);
		close(fd);
		umask(old_umask);
		return NULL;
	}
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		log_warn("ictrl_init: chmod");
		close(fd);
		(void)unlink(path);
		return NULL;
	}

	if (listen(fd, CONTROL_BACKLOG) == -1) {
		log_warn("ictrl_init: listen");
		close(fd);
		(void)unlink(path);
		return NULL;
	}

	socket_setblockmode(fd, 1);
	ctrl->fd = fd;
	ctrl->dispatch = dispatch;

	return ctrl;
}

void
ictrl_cleanup(struct ictrl_state *ctrl, char *path)
{
	if (path)
		unlink(path);

	event_del(&ctrl->ev);
	event_del(&ctrl->evt);
	close(ctrl->fd);
	free(ctrl);
}

void
ictrl_event_init(struct ictrl_state *ctrl)
{
	event_set(&ctrl->ev, ctrl->fd, EV_READ, ictrl_accept, NULL);
	event_add(&ctrl->ev, NULL);
	evtimer_set(&ctrl->evt, ictrl_accept, ctrl);
}

/* ARGSUSED */
void
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

	//TAILQ_INIT(&c->channel);
	c->fd = connfd;
	event_set(&c->ev, connfd, EV_READ, ctrl->dispatch, c);
	event_add(&c->ev, NULL);
}

void
ictrl_close(struct ictrl_state *ctrl, struct ictrl_session *c)
{
	event_del(&c->ev);
	close(c->fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&ctrl->evt, NULL)) {
		evtimer_del(&ctrl->evt);
		event_add(&ctrl->ev, NULL);
	}

	//pdu_free_queue(&c->channel);
	free(c);
}
