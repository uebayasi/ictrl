struct server_config;
struct server_ops;
struct server_context;

struct server_config {
	char *sockname;
	char *username;
	int exit_wait;
	int verbose;
	int debug;
	int nobkill;
	struct server_ops *ops;
};

struct server_ops {
	void (*init)(struct server_context *);
	void (*start)(struct server_context *);
	void (*stop)(struct server_context *);
	void (*shutdown)(struct server_context *);
	int (*isdown)(struct server_context *);
};

struct server_context {
	struct server_config *config;
	void *data;
	struct event exit_ev;
	int exit_rounds;
};

struct server_context *
		server_init(struct server_config *, void *);
void		server_loop(struct server_context *);
