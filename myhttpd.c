/*
*	This is a simple http server wrote for practice.
*	Author: nearmeng
*	Time: 2015-03-18
*/

#include "sys/time.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "sys/param.h"
#include "sys/uio.h"

#include "errno.h"
#include "fcntl.h"
#include "pwd.h"
#include "signal.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "syslog.h"
#include "unistd.h"

#include "timer.h"
#include "fdwatch.h"
#include "config.h"
#include "libhttpd.h"
#include "mmc.h"

static char* argv0;
static int port;
static char* dir;
static int do_chroot;
static int no_symlinks;
static int do_vhost;
static char* cgi_pattern;
static char* logfile;
static char* hostname;
static char* pidfile;
static char* user;

typedef struct 
{
	int conn_state;
	httpd_conn* hc; //has to define
	long limit;
	time_t started_at;
	int numtnums;
	Timer* idle_read_timer;
	Timer* idle_send_timer;
	Timer* wakeup_timer;
	Timer* linger_timer;

	off_t bytes;
	off_t bytes_sent;
	off_t bytes_to_send;
} conn_tab;

static conn_tab* conn;
static int num_connnects, max_connects;

//connect state define 
#define CNST_FREE 0
#define CNST_READING 1
#define CNST_SENDING 2
#define CNST_PAUSING 3
#define CNST_LINGERING 4

static httpd_server* hs = NULL;
static int terminate = 0;
static int fdwatch_recompute;


//function declare
static void parse_args(int argc, char** argv);
static void usage(void);
static void shut_down(void);
static int handle_new_connect(struct timeval* tv);
static void handle_read(conn_tab* c, struct timeval* tv);
static void handle_send(conn_tab* c, struct timeval* tv);
static void handle_linger(conn_tab* c, struct timeval* tv);

static void idle_read_timer_callback(timeout_args args, struct timeval* tv);
static void idle_send_timer_callback(timeout_args args, struct timeval* tv);
static void wakeup_timer_callback(timeout_args args, struct timeval* tv);
static void occasional_callback(timeout_args args, struct timeval* tv);

static void clear_connection(conn_tab* c, struct timeval* tv);
static void really_clear_connection(conn_tab* t, struct  timeval* tv);


static void handle_term(int sig)
{
	shut_down();
	syslog(LOG_NOTICE, "exiting due to signal %d", sig);
	closelog();
	exit(1);
}

//处理log文件
static void handle_hup(int sig)
{
	FILE* logfp;

	if(logfile != NULL)
	{
		logfp = fopen(logfile, "a");
		if(logfp == NULL)
		{
			syslog(LOG_CRIT, "reopening %.80s - %m", logfile);
			return;
		}
		fcntl(fileno(logfp), F_SETFD, 1);
		httpd_set_logfp(hs, logfp);
	}
}

static void handle_usr1(int sig)
{
	terminate = 1;
	if(hs != NULL)
	{
		httpd_server* ths = hs;
		hs = NULL;
		httpd_terminate(ths);
	}
}

int main(int argc, char* argv[])
{
	char* cp;
	struct passwd* pwd;
	uid_t uid;
	gid_t gid;
	char cwd[MAXPATHLEN];
	FILE* logfp;
	int num_ready;
	int cnum;
	conn_tab* c;
	httpd_conn* hc;
	u_int addr;
	struct timeval tv;
	struct hostent* he;


	//get the program name
	argv0 = argv[0];

	cp = strrchr(argv0, '/');
	if(cp != NULL)
	{
		cp++;
	}
	else
		cp = argv0;

	//open the log
	openlog(cp, LOG_NDELAY|LOG_PID, 0);

	//parse the comand-line
	parse_args(argc, argv);

	//time zone set
	tzset();

	//check hostname
	if(hostname == NULL)
	{
		addr = htonl(INADDR_ANY);
	}
	else
	{
		addr = inet_addr(hostname);
		if(addr == -1)
		{
			he = gethostbyname(hostname);
			if(he == NULL)
			{
				syslog(LOG_CRIT, "gethostbyname %.80s - %m", hostname);
				exit(1);
			}
			if(he->h_addrtype != AF_INET || he->h_length != sizeof(addr))
			{
				syslog(LOG_CRIT, "non-ip address %.80s - %m", hostname);
				exit(1);
			}
			memcpy(&addr, he->h_addr, sizeof(addr));
		}
	}

	//check port
	if(port <= 0)
	{
		syslog(LOG_CRIT, "illegal port number");
		fprintf(stderr, "illegal port number");

		exit(1);
	}

	//log file
	if(logfile != NULL)
	{
		logfp = fopen(logfile, "a");
		if(logfp == NULL)
		{
			syslog(LOG_CRIT,  "%.80s - %m", logfile);
			exit(1);
		}
		fcntl(fileno(logfp), F_SETFD, 1);
	}
	else
	{
		logfp = NULL;
	}

	//user info
	pwd = getpwnam(user);
	if(pwd == NULL)
	{
		syslog(LOG_CRIT, "unknow user %.80s - %m", user);
		exit(1);
	}
	uid = pwd->pw_uid;
	gid = pwd->pw_gid;

	//switch dirs
	if(dir != NULL)
	{
		if(chdir(dir) < 0)
		{
			syslog(LOG_CRIT, "chdir - %m");
			perror("chdir");
			exit(1);
		}
	}
	/*
	else if(getuid() == 0)
	{
		if(chdir(pwd->pw_dir) < 0)
		{
			syslog(LOG_CRIT, "chdir - %m");
			perror("chdir");
			exit(1);
		}
	}
	*/

	//get current dir, and guarantee to end with '/'
	getcwd(cwd, sizeof(cwd)-1);
	if(cwd[strlen(cwd)-1] != '/')
	{
		strcat(cwd, "/");
	}

#ifdef DEBUG
	//daemon
	if(daemon(1, 1) < 0)
	{
		syslog(LOG_CRIT, "daemon - %m");
		exit(1);
	}
#endif

	//pid file
	if(pidfile != NULL)
	{
		FILE* pidfp = fopen(pidfile, "w");
		if(pidfp == NULL)
		{
			syslog(LOG_CRIT, "%.80s - %m", logfile);
			exit(1);
		}
		fprintf(pidfp, "%d\n", getpid());
		fclose(pidfp);
	}

	//chroot
	if(do_chroot)
	{
		if(chroot(cwd) < 0)
		{
			syslog(LOG_CRIT, "chroot - %m");
			perror("chroot");
			exit(1);
		}
		//after chroot, the current dir is /
		strcpy(cwd, "/");
	}

	//set signal proc funcs
	(void) signal(SIGTERM, handle_term);
	(void) signal(SIGINT, handle_term);
	(void) signal(SIGPIPE, SIG_IGN);
	(void) signal(SIGHUP, handle_hup);
	(void) signal(SIGUSR1, handle_usr1);

	//ok, set the http server 
	hs = httpd_initialize(hostname, addr, port, cgi_pattern,
		cwd, logfp, no_symlinks, do_vhost);
	if(hs == NULL)
	{
		syslog(LOG_CRIT, "http server init - %m");
		exit(1);
	}

	//set the occasional timer to clean the free timer if we have time
	tmr_create(&occasional_callback, (timeout_args)0, NULL, OCCASIONAL_TIME * 1000L, 1);

	//give up the root 
	if(getuid() == 0)
	{
		if(setgroups(0, NULL) < 0)
		{
			syslog(LOG_CRIT, "setgroups - %m");
			exit(1);
		}
		if(setgid(gid) < 0)
		{
			syslog(LOG_CRIT, "set gid - %m");
			exit(1);
		}
		if(initgroups(user, gid) < 0)
		{
			syslog(LOG_CRIT, "initgroups - %m");
			exit(1);
		}
		setlogin(user);
		if(setuid(uid) < 0)
		{
			syslog(LOG_CRIT, "setuid - %m");
			exit(1);
		}
		if(do_chroot == 0)
		{
			syslog(LOG_CRIT, "start without chroot");
		}
	}

	//init connection poll(array)
	max_connects = fdwatch_get_nfiles();
	if(max_connects < 0)
	{
		syslog(LOG_CRIT, "fdwatch init error");
		exit(1);
	}
	max_connects -= SPARE_FDS;
	conn = NEW(conn_tab, max_connects);
	if(conn == NULL)
	{
		syslog(LOG_CRIT, "out of memory");
		exit(1);
	}
	for(cnum = 0; cnum < max_connects; cnum++)
	{
		conn[cnum].conn_state = CNST_FREE;
		conn[cnum].hc = NULL;
		conn[cnum].limit = 1234567890L;
	}
	num_connnects = 0;
	fdwatch_recompute = 1;

	//now, begin the mainloop
	(void) gettimeofday(&tv, NULL);
	while(terminate == 0 || num_connnects > 0)
	{
		if(fdwatch_recompute)
		{
			fdwatch_clear();
			if(num_connnects > 0)
			{
				for(cnum = 0; cnum < max_connects; cnum++)
				{
					switch(conn[cnum].conn_state)
					{
						case CNST_READING:
						case CNST_LINGERING:
							fdwatch_add_fd(conn[cnum].hc->conn_fd, FD_READ);
							break;
						case CNST_SENDING:
							fdwatch_add_fd(conn[cnum].hc->conn_fd, FD_WRITE);
							break;
					}
				}
			}
			if(hs != NULL)
			{
				fdwatch_add_fd(hs->listen_fd, FD_READ);
			}
			fdwatch_recompute = 0;
		}
		//begin fd watch
		num_ready = fdwatch(tmr_timeout_ms(&tv));
		if(num_ready < 0)
		{
			if(errno == EINTR)
				continue;
			syslog(LOG_ERR, "fdwatch - %m");
			exit(1);
		}
		gettimeofday(&tv, NULL);
		if(num_ready == 0)
		{
			tmr_run(&tv);
			continue;
		}
		if(hs != NULL && fdwatch_check_fd(hs->listen_fd, FD_READ))
		{
			num_ready--;
			if(handle_new_connect(&tv))
				continue;    //new connect first
		}
		for(cnum = 0; num_ready > 0 && cnum < max_connects; cnum++)
		{
			c = &conn[cnum];
			hc = c->hc;

			if(c->conn_state == CNST_READING &&
				fdwatch_check_fd(hc->conn_fd, FD_READ))
			{
				num_ready--;
				handle_read(c, &tv);
			}
			else if(c->conn_state == CNST_SENDING &&
				fdwatch_check_fd(hc->conn_fd, FD_WRITE))
			{
				num_ready--;
				handle_send(c, &tv);
			}
			else if(c->conn_state == CNST_LINGERING &&
				fdwatch_check_fd(hc->conn_fd, FD_READ))
			{
				num_ready--;
				handle_linger(c, &tv);
			}
		}
		tmr_run(&tv);
	}

	//end
	shut_down();
	syslog(LOG_NOTICE, "exiting");
	closelog();
	exit(0);
}


static void parse_args(int argc, char** argv)
{
	int argn;

	port = DEFAULT_PORT;
	dir = NULL;
	do_chroot = DEFAULT_CHROOT_SET;
	no_symlinks = do_chroot;
	do_vhost = DEFAULT_VHOST_SET;
	cgi_pattern = DEFAULT_CGI_PATTERN;
	hostname = NULL;
	logfile = NULL;
	pidfile = NULL;
	user = DEFAULT_USER;

	argn = 1;
	while(argn < argc && argv[argn][0]=='-')
	{
		/*
		if(strcmp(argv[argn], "-C") == 0)
		{
			++argn;
			read_config(argv[argn]);
		}
		else 
		*/
		if(strcmp(argv[argn], "-p") == 0)
		{
			++argn;
			port = atoi(argv[argn]);
		}
		else if(strcmp(argv[argn], "-d") == 0)
		{
			++argn;
			dir = argv[argn];
		}
		else if(strcmp(argv[argn], "-r") == 0)
		{
			do_chroot = 1;
			no_symlinks = 1;
		}
		else if(strcmp(argv[argn], "-nor") == 0)
		{
			do_chroot = 0;
			no_symlinks = 0;
		}
		else if(strcmp(argv[argn], "-u") == 0)
		{
			++argn;
			user = argv[argn];
		}
		else if(strcmp(argv[argn], "-c") == 0)
		{
			++argn;
			cgi_pattern = argv[argn];
		}
		else if(strcmp(argv[argn], "-h") == 0)
		{
			++argn;
			hostname = argv[argn];
		}
		else if(strcmp(argv[argn], "-l") == 0)
		{
			++argn;
			logfile = argv[argn];
		}
		else if(strcmp(argv[argn], "-v") == 0)
		{
			do_vhost = 1;
		}
		else if(strcmp(argv[argn], "-i") == 0)
		{
			++argn;
			pidfile = argv[argn];
		}
		else
			usage();

		++argn;
	}
	if(argn != argc)
		usage();
}

static void usage(void)
{
	fprintf(stderr, "usage: This is the usage of myhttpd");
	exit(1);
}

static void shut_down(void)
{
	int cnum;
	struct timeval tv;

	//close connection array
	for(cnum = 0; cnum < max_connects; cnum++)
	{
		if(conn[cnum].conn_state != CNST_FREE)
		{
			httpd_close_conn(conn[cnum].hc, &tv);
		}
		if(conn[cnum].hc != NULL)
		{
			httpd_destroy_conn(conn[cnum].hc);
		}
	}
	free(connect);
	//close server struct
	if(hs != NULL)
	{
		httpd_server* ths = hs;
		hs = NULL;
		httpd_terminate(ths);
	}
	//close other module 
	mmc_destroy();
	tmr_destroy();
}

static int handle_new_connect(struct timeval* tv)
{
	int cnum;
	conn_tab* c;
	timeout_args args;
	int flags;

	for(;;)
	{
		//check connection num
		if(num_connnects > max_connects)
		{
			syslog(LOG_WARNING, "too many connections");
			tmr_run(tv);
			return 0;
		}
		//find a free place in the connection table
		for(cnum = 0; cnum < max_connects; cnum++)
		{
			if(conn[cnum].conn_state == CNST_FREE)
			{
				break;
			}
		}
		c = &conn[cnum];
		if(c->hc == NULL)
		{
			c->hc = NEW(httpd_conn, 1);
			if(c->hc == NULL)
			{
				syslog(LOG_CRIT, "out of memory");
				exit(1);
			}
			c->hc->initialized = 0;
		}

		//get the connection
		switch(httpd_get_conn(hs, c->hc))
		{
			case GC_FAIL:
			case GC_NO_MORE:
				return 1;
		}
		c->conn_state = CNST_READING;
		fdwatch_recompute = 1;
		num_connnects++;
		args.p = c;
		c->idle_read_timer = tmr_create(&idle_read_timer_callback, args, tv, IDLE_READ_TIMELIMIT*1000, 0);
		c->wakeup_timer = NULL;
		c->idle_send_timer = NULL;
		c->linger_timer = NULL;
		c->bytes_sent = 0;
		c->numtnums = 0;

		//set connection fd to no-delay mode
		flags = fcntl(c->hc->conn_fd, F_GETFL, 0);
		if(flags == -1)
		{
			syslog(LOG_ERR, "fcntl F_GET");
		}
		if(fcntl(c->hc->conn_fd, F_SETFL, flags | O_NDELAY) < 0)
		{
			syslog(LOG_ERR, "fcntl F_SET");
		}
	}

}

static void handle_read(conn_tab* c, struct timeval* tv)
{
	int sz;
	timeout_args args;
	httpd_conn* hc = c->hc;

	//check the space
	if(hc->read_idx >= hc->read_size)
	{
		if(hc->read_size > 5000)
		{
			httpd_send_err(hc, 400, httpd_err400title, httpd_err400form, "");
			clear_connection(c, tv);
			return;
		}
		httpd_realloc_str(&hc->read_buf, &hc->read_size, hc->read_size + 1000);

	}

	//read some data form conn
	sz = read(hc->conn_fd, &(hc->read_buf[hc->read_idx]), hc->read_size-hc->read_idx);
	if(sz < 0)
	{
		httpd_send_err(hc, 400, httpd_err400title, httpd_err400form, "");
		clear_connection(c, tv);
		return;
	}
	hc->read_idx += sz;

	//check the request from user
	switch(httpd_got_request(hc))
	{
		case GR_NO_REQUEST:
			return;
		case GR_BAD_REQUEST:
			httpd_send_err(hc, 400, httpd_err400title, httpd_err400form, "");
			clear_connection(c, tv);
			return;
	}

	//parse the request
	if(httpd_parse_request(hc) < 0)
	{
		clear_connection(c, tv);
		return;
	}

	//start response
	if(httpd_start_request(hc) < 0)
	{
		clear_connection(c, tv);
		return;
	}

	//Fill in bytes to send
	if(hc->got_range)
	{
		c->bytes_sent = hc->init_byte_loc;
		c->bytes_to_send = hc->end_byte_loc +1;
	}
	else
	{
		c->bytes_to_send = hc->bytes;
	}

	//check if it's already handled
	if(hc->file_address == NULL)
	{
		c->bytes_sent = hc->bytes;
		clear_connection(c, tv);
		return;
	}
	if(c->bytes_sent >= c->bytes_to_send)
	{
		clear_connection(c, tv);
		return;
	}

	//ok, begin to send
	c->conn_state = CNST_SENDING;
	fdwatch_recompute = 1;
	c->started_at = tv->tv_sec;
	args.p = c;
	tmr_cancle(c->idle_read_timer);
	c->idle_read_timer = NULL;
	c->idle_send_timer = tmr_create(&idle_send_timer_callback,
		args, tv, IDLE_SEND_TIMELIMIT*1000L, 0);
}

static void handle_send(conn_tab* c, struct timeval* tv)
{
	int sz, coast;
	timeout_args args;
	time_t elasped;
	httpd_conn* hc = c->hc;

	//check the response length
	if(hc->responselen == 0)
	{
		sz = write(hc->conn_fd, &(hc->file_address[c->bytes_sent]),
			MIN(c->bytes_to_send - c->bytes_sent, c->limit));
	}
	else
	{
		struct iovec iv[2];

		iv[0].iov_base = hc->response;
		iv[0].iov_len = hc->responselen;
		iv[1].iov_base = &(hc->file_address[c->bytes_sent]);
		iv[1].iov_len = MIN(c->bytes_to_send - c->bytes_sent, c->limit);
		sz = writev(hc->conn_fd, iv, 2);
	}

	if(sz == 0 || (sz < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)))
	{
		c->conn_state = CNST_PAUSING;
		fdwatch_recompute = 1;
		args.p = c;
		c->wakeup_timer = tmr_create(&wakeup_timer_callback,
			args, tv, BLOCK_DELAY_TIMELIMIT, 0);
		return;
	}
	if(sz < 0)
	{
		if(errno != EPIPE && errno != EINVAL)
		{
			syslog(LOG_ERR, "write - %m");
		}
		clear_connection(c, tv);
		return;
	}

	//ok, begin to write
	tmr_reset(c->idle_send_timer, tv);
	if(hc->responselen > 0)
	{
		if(sz < hc->responselen)
		{
			int newlen = hc->responselen - sz;
			memcpy(hc->response, &(hc->response[sz]), newlen);
			hc->responselen = newlen;
			sz = 0;
		}
		else
		{
			sz -= hc->responselen;
			hc->responselen = 0;
		}
	}

	c->bytes_sent += sz;

	if(c->bytes_sent >= c->bytes_to_send)
	{
		clear_connection(c, tv);
		return;
	}
}

static void handle_linger(conn_tab* c, struct timeval* tv)
{
	char buf[1024];
	int r;

	r = read(c->hc->conn_fd, buf, sizeof(buf));
	if(r <= 0)
	{
		really_clear_connection(c, tv);
	}
}

static void clear_connection(conn_tab* c, struct timeval* tv)
{
	timeout_args args;

	httpd_write_response(c->hc);

	//cancle the timers
	if(c->idle_read_timer != NULL)
	{
		tmr_cancle(c->idle_read_timer);
		c->idle_read_timer = NULL;
	}
	if(c->idle_send_timer != NULL)
	{
		tmr_cancle(c->idle_send_timer);
		c->idle_send_timer = NULL;
	}
	if(c->wakeup_timer != NULL)
	{
		tmr_cancle(c->wakeup_timer);
		c->wakeup_timer = NULL;
	}

	really_clear_connection(c, tv);
}

static void really_clear_connection(conn_tab* c, struct  timeval* tv) 
{
	httpd_close_conn(c->hc, tv);
	if(c->linger_timer != NULL)
	{
		tmr_cancle(c->linger_timer);
		c->linger_timer = NULL;
	}
	c->conn_state = CNST_FREE;
	fdwatch_recompute = 1;
	num_connnects--;
}


//timer callbacks
static void idle_read_timer_callback(timeout_args args, struct timeval* tv)
{
	conn_tab* c;

	c= (conn_tab*)args.p;
	c->idle_read_timer = NULL;
	if(c->conn_state != CNST_FREE)
	{
		syslog(LOG_INFO, "%.80s connnection time out reading", inet_ntoa(c->hc->client_addr));
		httpd_send_err(c->hc, 408, httpd_err408title, httpd_err408form, "");
		clear_connection(c, tv);
	}
}

static void idle_send_timer_callback(timeout_args args, struct timeval* tv)
{
	conn_tab* c;
	
	c= (conn_tab*)args.p;
	c->idle_send_timer = NULL;
	if(c->conn_state != CNST_FREE)
	{
		syslog(LOG_INFO, "%.80s connnection time out sending", inet_ntoa(c->hc->client_addr));
		clear_connection(c, tv);
	}
}

static void wakeup_timer_callback(timeout_args args, struct timeval* tv)
{
	conn_tab* c;

	c = (conn_tab*)args.p;
	c->wakeup_timer = NULL;

	if(c->conn_state == CNST_PAUSING)
	{
		c->conn_state = CNST_SENDING;
		fdwatch_recompute = 1;
	}
}

static void linger_timer_callback(timeout_args args, struct timeval* tv)
{
	conn_tab* c;

	c = (conn_tab*)args.p;
	c->linger_timer = NULL;
	really_clear_connection(c, tv);
}

static void occasional_callback(timeout_args args, struct timeval* tv)
{
	mmc_cleanup(tv);
	tmr_clean();
}

