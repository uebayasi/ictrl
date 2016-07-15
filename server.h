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
