LIB=	ictrl
SRCS=	buf.c \
	ictrl.c \
	client.c \
	server.c \
	test.c \

NOMAN=	1
NOLINT=	1

.include <bsd.lib.mk>
