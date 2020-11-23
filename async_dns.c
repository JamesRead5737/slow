#include <ares.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <hiredis/hiredis.h>

#define MAXWAITING 1000 /* Max. number of parallel DNS queries */
#define MAXTRIES      3 /* Max. number of tries per domain */
#define TIMEOUT    3000 /* Max. number of ms for first try */

#define SERVERS    "1.0.0.1,8.8.8.8" /* DNS server to use (Cloudflare & Google) */

redisContext *ctx = NULL;
static int nwaiting;

static void
state_cb(void *data, int s, int read, int write)
{
	//printf("Change state fd %d read:%d write:%d\n", s, read, write);
}

static void
callback(void *arg, int status, int timeouts, struct hostent *host)
{
	nwaiting--;
	char *host_original = (char *)arg;

	if(!host || status != ARES_SUCCESS){
		//fprintf(stderr, "Failed to lookup %s\n", ares_strerror(status));
		return;
	}

	char ip[INET6_ADDRSTRLEN];

	//for (int i = 0; host->h_addr_list[i]; ++i) {
		//inet_ntop(host->h_addrtype, host->h_addr_list[i], ip, sizeof(ip));
	if (host->h_addr_list[0] != NULL){
		inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof(ip));
		redisReply *reply;
		//reply = redisCommand(ctx, "SET dns:%s %s NX", host_original, ip);
		reply = redisCommand(ctx, "SET %s %s NX", host->h_name, ip);
		//printf("%s %s\n", ip, host->h_name);
	}
}

static void
wait_ares(ares_channel channel)
{
	struct timeval *tvp, tv;
	fd_set read_fds, write_fds;
	int nfds;

	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	nfds = ares_fds(channel, &read_fds, &write_fds);

	if (nfds > 0) {
		tvp = ares_timeout(channel, NULL, &tv);
		select(nfds, &read_fds, &write_fds, NULL, tvp);
		ares_process(channel, &read_fds, &write_fds);
	}
}

int
main(int argc, char *argv[])
{
	FILE * fp;
	char domain[128];
	size_t len = 0;
	ssize_t read;
	ares_channel channel;
	int status, done = 0;
	int optmask;

	ctx = redisConnect("127.0.0.1", 6379);
	if (ctx == NULL || ctx->err) {
		if (ctx) {
			fprintf(stderr, "Error: %s\n", ctx->errstr);
			return (-1);
		} else {
			fprintf(stderr, "Can't allocate redis context\n");
			return (-1);
		}
	}

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS) {
		printf("ares_library_init: %s\n", ares_strerror(status));
		return 1;
	}

	struct ares_options options = {
		.timeout = TIMEOUT,     /* set first query timeout */
		.tries = MAXTRIES       /* set max. number of tries */
	};
	optmask = ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES;

	status = ares_init_options(&channel, &options, optmask);
	if (status != ARES_SUCCESS) {
		printf("ares_init_options: %s\n", ares_strerror(status));
		return 1;
	}

	status = ares_set_servers_csv(channel, SERVERS);
	if (status != ARES_SUCCESS) {
		printf("ares_set_servers_csv: %s\n", ares_strerror(status));
		return 1;
	}
	
	
	fp = fopen(argv[1], "r");
	if (!fp)
		exit(EXIT_FAILURE);

	do {
		if (nwaiting >= MAXWAITING || done) {
			do {
				wait_ares(channel);
			} while (nwaiting > MAXWAITING);
		}

		if (!done) {
			if (fscanf(fp, "%127s", domain) == 1) {
				ares_gethostbyname(channel, domain, AF_INET, callback, NULL);
				nwaiting++;
			} else {
				fprintf(stderr, "done sending\n");
				done = 1;
			}
		}
//		printf("nwaiting=%d\n",nwaiting);
	} while (nwaiting > 0);

	ares_destroy(channel);
	ares_library_cleanup();
	
	fclose(fp);

	return 0;
}
