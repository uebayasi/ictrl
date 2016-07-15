#ifndef _ICTRL_ICTRL_H_
#define _ICTRL_ICTRL_H_

#include <sys/queue.h>

#include <event.h>

#include "buf.h"

struct ictrl_config;
struct ictrl_session;
struct ictrl_state;
struct pdu;
struct ctrlmsghdr;
struct ctrldata;

struct ictrl_config {
	char			*path;
	void			(*proc)(struct ictrl_session *,
				    struct pdu *);
};

struct ictrl_session {
	struct ictrl_state	*state;
	struct event		ev;
	struct pduq		channel;
	int			fd;	/* accept fd */
	char			buf[CONTROL_READ_SIZE];
};

struct ictrl_state {
	struct ictrl_config	*config;
	struct event		ev;
	struct event		evt;
	int			fd;	/* socket fd */
};

struct ictrl_state *
		ictrl_init(struct ictrl_config *);
void		ictrl_fini(struct ictrl_state *);
void		ictrl_start(struct ictrl_state *);
void		ictrl_stop(struct ictrl_state *);

struct ictrl_session *
		ictrl_client_init(struct ictrl_config *);
void		ictrl_client_close(struct ictrl_session *);

int		ictrl_compose(void *, u_int16_t, void *, size_t);
int		ictrl_build(void *, u_int16_t, int, struct ctrldata *);
int		ictrl_send(struct ictrl_session *);
struct pdu	*ictrl_recv(struct ictrl_session *);

/*
 * Common control message header.
 * A message can consist of up to 3 parts with specified length.
 */
struct ctrlmsghdr {
	u_int16_t	type;
	u_int16_t	len[3];
};

struct ctrldata {
	void		*buf;
	size_t		 len;
};

#define CTRLARGV(x...)	((struct ctrldata []){ x })

#endif /* _ICTRL_ICTRL_H_ */
