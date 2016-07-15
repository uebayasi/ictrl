#include <sys/queue.h>

#include <event.h>

//#include "buf.h"

struct ictrl_config;
struct ictrl_session;
struct ictrl_state;
struct pdu;

struct ictrl_config {
	char *path;
	void (*proc)(struct ictrl_session *, struct pdu *);
};

struct ictrl_session {
	struct ictrl_state	*state;
	struct event		ev;
	struct pduq		channel;
	int			fd;
	char			buf[CONTROL_READ_SIZE];
};

struct ictrl_state {
	struct event		ev;
	struct event		evt;
	int			fd;
	void			(*procpdu)(struct ictrl_session *,
				    struct pdu *);
};

struct ictrl_state *
		ictrl_init(char *,
		    void (*)(struct ictrl_session *, struct pdu *));
void		ictrl_cleanup(struct ictrl_state *, char *);
void		ictrl_event_init(struct ictrl_state *);

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
