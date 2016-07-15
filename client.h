struct client_config {
	char *sockname;
};

struct client_state {
	struct client_config *config;
	struct pduq	channel;
	int		fd;
};

struct client_state *ictrl_connect(struct client_config *);
void ictrl_close(struct client_state *);
void ictrl_queue(void *ch, struct pdu *pdu);
int ctl_sendpdu(int fd, struct pdu *pdu);
struct pdu *ctl_recvpdu(int fd, char *buf, size_t buflen);
