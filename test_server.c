/*
 * Copyright (c) 2016 Masao Uebayashi <uebayasi@tombiinc.com>
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

#include <sys/param.h>	/* nitems */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buf.h"
#include "ictrl.h"
#include "server.h"

struct test_context {
	struct ictrl_config *ctrl_cf;
	struct ictrl_state *ctrl;
};

__dead void	usage(void);

void test_server_init(void *);
void test_server_fini(void *);
void test_server_start(void *);
void test_server_stop(void *);
void test_server_shutdown(void *);
int test_shutdown_isdown(void *);
void test_ictrl_proc(struct ictrl_session *, struct pdu *);

int
main(int argc, char *argv[])
{
	int ch;

	struct server_context *ctx;
	struct server_ops ops = {
		.init = test_server_init,
		.fini = test_server_fini,
		.start = test_server_start,
		.stop = test_server_stop,
		.shutdown = test_server_shutdown,
		.isdown = test_shutdown_isdown
	};
	struct server_config cf = {
		.sockname = "/var/run/hoge.sock",
		.username = "_hoge",
		.exit_wait = 10,
		.verbose = 0,
		.debug = 0,
		.nobkill = 0,
		.ops = &ops
	};
	struct ictrl_config ctrl_cf = {
		.path = "/var/run/hoge.sock",
		.backlog = 5,
		.proc = test_ictrl_proc
	};
	struct test_context test = {
		.ctrl_cf = &ctrl_cf
	};

	while ((ch = getopt(argc, argv, "ds:u:vw:")) != -1) {
		switch (ch) {
		case 'd':
			cf.debug = 1;
			break;
		case 's':
			cf.sockname = optarg;
			ctrl_cf.path = optarg;
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

	ctx = server_init(&cf, &test);

	server_loop(ctx);

	server_fini(ctx);

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
test_server_init(void *data)
{
	struct test_context *test = data;

	test->ctrl = ictrl_server_init(test->ctrl_cf);
}

void
test_server_fini(void *data)
{
	struct test_context *test = data;

	ictrl_server_fini(test->ctrl);
}

void
test_server_start(void *data)
{
	struct test_context *test = data;

	ictrl_server_start(test->ctrl);
}

void
test_server_stop(void *data)
{
	struct test_context *test = data;

	ictrl_server_stop(test->ctrl);
}

void
test_server_shutdown(void *data)
{
	struct test_context *test = data;

	// XXX
}

int
test_shutdown_isdown(void *data)
{
	struct test_context *test = data;

	// XXX

	return 1;
}

void
test_ictrl_proc(struct ictrl_session *c, struct pdu *pdu)
{
	struct ictrl_msghdr *cmh;

	cmh = pdu_getbuf(pdu, NULL, 0);
	if (cmh == NULL)
		goto done;

	switch (cmh->type) {
	case 123:
		ictrl_compose(c, 456, NULL, 0);
		break;
	default:
		break;
	}

done:
	pdu_free(pdu);
}
