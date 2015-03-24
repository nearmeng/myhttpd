#ifndef _CONFIG_H_
#define _CONFIG_H_

#define SERVER_SOFTWARE "hxhttpd"

#define SERVER_ADDRESS "nearmeng.com"

#define CGI_PATH "/usr/local/bin:/usr/ucb:/bin:/usr/bin"

#define CGI_BYTECOUNT 50000

#define GENERATE_INDEXES 1

#define INDEX_NAMES "index.html", "index.htm", "default.htm", "index.cgi"

#define MAX_LINKS 10

#define LISTEN_BACKLOG 5

#define DEFAULT_PORT 80

#define DEFAULT_CHROOT_SET 1

#define DEFAULT_VHOST_SET 0

#define DEFAULT_CGI_PATTERN "/cgi-bin/*"

#define DEFAULT_USER "nobody"

#define OCCASIONAL_TIME 300

#define IDLE_SEND_TIMELIMIT 5000

#define IDLE_READ_TIMELIMIT 5000

#define BLOCK_DELAY_TIMELIMIT 1000

#define SPARE_FDS 10

#define MIN_REAP_TIME  100

#define MAX_REAP_TIME  100

#endif
