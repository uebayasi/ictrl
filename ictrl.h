#ifndef _ICTRL_ICTRL_H_
#define _ICTRL_ICTRL_H_

#include <sys/queue.h>

#include <event.h>

#include "buf.h"

struct ictrl_config;
struct ictrl_session;
struct ictrl_state;
struct pdu;
struct ictrl_msghdr;

struct ictrl_config {
	char			*path;
	int			backlog;
	void			(*proc)(struct ictrl_session *,
				    struct pdu *);
};

struct ictrl_session {
	struct ictrl_state	*state;
	struct pduq		channel;
	char			buf[CONTROL_READ_SIZE];
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
struct pdu	*ictrl_recv(struct ictrl_session *);

/*
 * Common control message header.
 * A message can consist of up to 3 parts with specified length.
 */

struct ictrl_msghdr {
	u_int16_t	type;
	u_int16_t	len[3];
};

#endif /* _ICTRL_ICTRL_H_ */
