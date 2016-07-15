#include <stdio.h>

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

	c = ictrl_client_init(&config);

	// {
	ictrl_compose(c, 123, msg, 5);
	ictrl_send(c);
	pdu = ictrl_recv(c);
	cmh = pdu_getbuf(pdu, NULL, 0);
	switch (cmh->type) {
	case 456:
		printf("success!\n");
		break;
	default:
		return 1;
	}
	pdu_free(pdu);
	// }

	ictrl_client_fini(c);

	return 0;
}
