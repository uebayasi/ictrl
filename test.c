#include <sys/param.h>	/* nitems */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"

__dead void	usage(void);

void test_init(struct server_context *);
void test_start(struct server_context *);
void test_stop(struct server_context *);
void test_shutdown(struct server_context *);
int test_isdown(struct server_context *);

int
main(int argc, char *argv[])
{
	int ch;

	struct server_context *ctx;
	struct server_ops ops = {
		.init = test_init,
		.start = test_start,
		.stop = test_stop,
		.shutdown = test_shutdown,
		.isdown = test_isdown
	};
	struct server_config cf = {
		.sockname = "/var/run/hoge.sock",
		.username = "hoge",
		.exit_wait = 10,
		.verbose = 0,
		.debug = 0,
		.nobkill = 0,
		.ops = &ops
	};

	while ((ch = getopt(argc, argv, "ds:u:vw:")) != -1) {
		switch (ch) {
		case 'd':
			cf.debug = 1;
			break;
		case 's':
			cf.sockname = optarg;
			break;
		case 'u':
			cf.username = optarg;
			break;
		case 'v':
			cf.verbose = 1;
			break;
		case 'w':
			cf.exit_wait = atoi(optarg);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	ctx = server_init(&cf, NULL);

	server_loop(ctx);

	return 0;
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dv] [-n device] [-s socket]\n",
	    __progname);
	exit(1);
}

void
test_init(struct server_context *ctx)
{
}

void
test_start(struct server_context *ctx)
{
}

void
test_stop(struct server_context *ctx)
{
}

void
test_shutdown(struct server_context *ctx)
{
}

int
test_isdown(struct server_context *ctx)
{
	return 0;
}
