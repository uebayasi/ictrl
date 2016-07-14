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

struct control {
	struct event		ev;
	//struct pduq		channel;
	int			fd;
};

struct control_state {
	struct event		ev;
	struct event		evt;
	int			fd;
} *control_state;

extern void (*control_dispatch)(int, short, void *);

#define	CONTROL_BACKLOG	5

void	control_accept(int, short, void *);
void	control_close(struct control *);

int
control_init(char *path)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask;

	if ((control_state = calloc(1, sizeof(*control_state))) == NULL) {
		log_warn("control_init: calloc");
		return -1;
	}

	if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
		log_warn("control_init: socket");
		return -1;
	}

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path)) {
		log_warnx("control_init: path %s too long", path);
		return -1;
	}

	if (unlink(path) == -1)
		if (errno != ENOENT) {
			log_warn("control_init: unlink %s", path);
			close(fd);
			return -1;
		}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("control_init: bind: %s", path);
		close(fd);
		umask(old_umask);
		return -1;
	}
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		log_warn("control_init: chmod");
		close(fd);
		(void)unlink(path);
		return -1;
	}

	if (listen(fd, CONTROL_BACKLOG) == -1) {
		log_warn("control_init: listen");
		close(fd);
		(void)unlink(path);
		return -1;
	}

	socket_setblockmode(fd, 1);
	control_state->fd = fd;

	return 0;
}

void
control_cleanup(char *path)
{
	if (path)
		unlink(path);

	event_del(&control_state->ev);
	event_del(&control_state->evt);
	close(control_state->fd);
	free(control_state);
}

void
control_event_init(void)
{
	event_set(&control_state->ev, control_state->fd, EV_READ,
	    control_accept, NULL);
	event_add(&control_state->ev, NULL);
	evtimer_set(&control_state->evt, control_accept, NULL);
}

/* ARGSUSED */
void
control_accept(int listenfd, short event, void *bula)
{
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 sun;
	struct control		*c;

	event_add(&control_state->ev, NULL);
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

			event_del(&control_state->ev);
			evtimer_add(&control_state->evt, &evtpause);
		} else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("control_accept");
		return;
	}

	if ((c = malloc(sizeof(struct control))) == NULL) {
		log_warn("control_accept");
		close(connfd);
		return;
	}

	//TAILQ_INIT(&c->channel);
	c->fd = connfd;
	event_set(&c->ev, connfd, EV_READ, control_dispatch, c);
	event_add(&c->ev, NULL);
}

void
control_close(struct control *c)
{
	event_del(&c->ev);
	close(c->fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&control_state->evt, NULL)) {
		evtimer_del(&control_state->evt);
		event_add(&control_state->ev, NULL);
	}

	//pdu_free_queue(&c->channel);
	free(c);
}
