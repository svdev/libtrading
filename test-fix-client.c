#include "trading/soupbin3_session.h"
#include "trading/itch41_message.h"
#include "trading/fix_message.h"
#include "trading/fix_session.h"
#include "trading/array.h"
#include "trading/die.h"

#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>

struct protocol_info {
	const char		*name;
	int			(*session_initiate)(const struct protocol_info *, int);
};

static int fix_session_initiate(const struct protocol_info *proto, int sockfd)
{
	struct fix_session *session;
	enum fix_version version;
	int retval;

	version = FIX_4_4;

	if (!strcmp(proto->name, "fix42"))
		version = FIX_4_2;
	else if (!strcmp(proto->name, "fix43"))
		version = FIX_4_3;
	else if (!strcmp(proto->name, "fix44"))
		version = FIX_4_4;

	session	= fix_session_new(sockfd, version, "BUYSIDE", "SELLSIDE");
	if (!session)
		die("unable to allocate memory for session");

	if (fix_session_logon(session)) {
		printf("Logon OK\n");
		retval = 0;
	} else {
		printf("Logon FAILED\n");
		retval = 1;
	}

	if (fix_session_logout(session)) {
		printf("Logout OK\n");
		retval = 0;
	} else {
		printf("Logout FAILED\n");
		retval = 1;
	}

	fix_session_free(session);

	return retval;
}

static const struct protocol_info protocols[] = {
	{ "fix",		fix_session_initiate },
	{ "fix42",		fix_session_initiate },
	{ "fix43",		fix_session_initiate },
	{ "fix44",		fix_session_initiate },
};

static const struct protocol_info *lookup_protocol_info(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(protocols); i++) {
		const struct protocol_info *proto_info = &protocols[i];

		if (!strcmp(proto_info->name, name))
			return proto_info;
	}
	return NULL;
}

static void usage(void)
{
	printf("\n  usage: trade client [hostname] [port] [protocol]\n\n");
	exit(EXIT_FAILURE);
}

static int socket_setopt(int sockfd, int level, int optname, int optval)
{
	return setsockopt(sockfd, level, optname, (void *) &optval, sizeof(optval));
}

int main(int argc, char *argv[])
{
	const struct protocol_info *proto_info;
	struct sockaddr_in sa;
	int saved_errno = 0;
	struct hostent *he;
	const char *proto;
	const char *host;
	int sockfd = -1;
	int retval;
	char **ap;
	int port;

	if (argc != 4)
		usage();

	host	= argv[1];
	port	= atoi(argv[2]);
	proto	= argv[3];

	proto_info = lookup_protocol_info(proto);
	if (!proto_info) {
		printf("Unsupported protocol '%s'\n", proto);
		exit(EXIT_FAILURE);
	}

	he = gethostbyname(host);
	if (!he)
		error("Unable to look up %s (%s)", host, hstrerror(h_errno));

	for (ap = he->h_addr_list; *ap; ap++) {
		sockfd = socket(he->h_addrtype, SOCK_STREAM, IPPROTO_TCP);
		if (sockfd < 0) {
			saved_errno = errno;
			continue;
		}

		sa = (struct sockaddr_in) {
			.sin_family		= he->h_addrtype,
			.sin_port		= htons(port),
		};
		memcpy(&sa.sin_addr, *ap, he->h_length);

		if (connect(sockfd, (const struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0) {
			saved_errno = errno;
			close(sockfd);
			sockfd = -1;
			continue;
		}
		break;
	}

	if (sockfd < 0)
		error("Unable to connect to a socket (%s)", strerror(saved_errno));

	if (socket_setopt(sockfd, IPPROTO_TCP, TCP_NODELAY, 1) < 0)
		die("cannot set socket option TCP_NODELAY");

	retval = proto_info->session_initiate(proto_info, sockfd);

	shutdown(sockfd, SHUT_RDWR);

	if (close(sockfd) < 0)
		die("close");

	return retval;
}