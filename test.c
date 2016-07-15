#include <sys/param.h>	/* nitems */
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <err.h>
#include <event.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void		main_sig_handler(int, short, void *);
__dead void	usage(void);
void		shutdown_cb(int, short, void *);

int
main(int argc, char *argv[])
{
	struct event ev_sigint, ev_sigterm, ev_sighup;
	struct passwd *pw;
	char *ctrlsock = ISCSID_CONTROL;
	char *vscsidev = ISCSID_DEVICE;
	int name[] = { CTL_KERN, KERN_PROC_NOBROADCASTKILL, 0 };
	int ch, debug = 0, verbose = 0, nobkill = 1;

	log_init(1);    /* log to stderr until daemonized */
	log_verbose(1);

	while ((ch = getopt(argc, argv, "dn:s:v")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'n':
			vscsidev = optarg;
			break;
		case 's':
			ctrlsock = optarg;
			break;
		case 'v':
			verbose = 1;
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

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	log_init(debug);
	log_verbose(verbose);

	if (control_init(ctrlsock) == -1)
		fatalx("control socket setup failed");

	if (!debug)
		daemon(1, 0);
	log_info("startup");

	name[2] = getpid();
	if (sysctl(name, 3, NULL, 0, &nobkill, sizeof(nobkill)) != 0)
		fatal("sysctl");

	event_init();
	vscsi_open(vscsidev);

	/* chroot and drop to iscsid user */
	if ((pw = getpwnam(ISCSID_USER)) == NULL)
		errx(1, "unknown user %s", ISCSID_USER);

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, main_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, main_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	control_event_init();
	initiator = initiator_init();

	event_dispatch();

	/* do some cleanup on the way out */
	control_cleanup(ctrlsock);
	initiator_cleanup(initiator);
	log_info("exiting.");
	return 0;
}

void
shutdown_cb(int fd, short event, void *arg)
{
	struct timeval tv;

	if (exit_rounds++ >= ISCSI_EXIT_WAIT || initiator_isdown(initiator))
		event_loopexit(NULL);

	timerclear(&tv);
	tv.tv_sec = 1;

	if (evtimer_add(&exit_ev, &tv) == -1)
		fatal("shutdown_cb");
}

void
main_sig_handler(int sig, short event, void *arg)
{
	struct timeval tv;

	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
	case SIGHUP:
		initiator_shutdown(initiator);
		evtimer_set(&exit_ev, shutdown_cb, NULL);
		timerclear(&tv);
		if (evtimer_add(&exit_ev, &tv) == -1)
			fatal("main_sig_handler");
		break;
	default:
		fatalx("unexpected signal");
		/* NOTREACHED */
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dv] [-n device] [-s socket]\n",
	    __progname);
	exit(1);
}
