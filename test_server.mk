PROG=	test_server
SRCS=	test_server.c

LDADD=	-L. -lictrl \
	-levent \
	-lutil \

NOMAN=	1
NOLINT=	1

.include <bsd.prog.mk>
