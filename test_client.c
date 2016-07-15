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

#include <stdio.h>
#include <unistd.h>

#include "buf.h"
#include "ictrl.h"

struct ictrl_config config = {
	.path = "/var/run/hoge.sock"
};

int
main(int argc, char *argv[])
{
	struct ictrl_session *c;
	char *msg = "hoge";
	struct pdu *pdu;
	struct ictrl_msghdr *cmh;
	int id = 1;
	int ch;

	while ((ch = getopt(argc, argv, "n:s:")) != -1) {
		switch (ch) {
		case 'n':
			id = atoi(optarg);
			break;
		case 's':
			config.path = optarg;
			break;
		}
	}

	c = ictrl_client_init(&config);

	// {
	ictrl_compose(c, id, msg, 5);
	ictrl_send(c);
	pdu = ictrl_recv(c);
	cmh = pdu_getbuf(pdu, NULL, 0);
	if (cmh->type == id * 10) {
		printf("success!\n");
	} else {
		return 1;
	}
	pdu_free(pdu);
	// }

	ictrl_client_fini(c);

	return 0;
}
