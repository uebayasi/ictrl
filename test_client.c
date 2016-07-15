#include "ictrl.h"

struct ictrl_config config = {
	.path = "/var/run/hoge.sock"
};

int
main(int argc, char *argv[])
{
	struct ictrl_session *client;

	client = ictrl_client_init(&config);

	// ictrl_compose();

	ictrl_client_close(client);

	return 0;
}
