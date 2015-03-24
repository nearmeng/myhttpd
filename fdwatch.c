#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/select.h>

#include "fdwatch.h"

#define MIN(a, b) a<b?a:b

//模块全局变量
static int nfiles;  //可以watch的最大fd数目
static int maxfd;   //当前watch的最大fd值
static int conn_nums;  //当前连接的数目
static long nselect;	//当前select的次数

//标记 set
static fd_set master_rfdset;   
static fd_set master_wfdset;
//工作 set
static fd_set working_rfdset;
static fd_set working_wfdset;

//select需要的静态函数
static int fdwatch_select(long timeout_msecs);

int fdwatch_get_nfiles( void )
{
#ifdef RLIMIT_NOFILE
	struct rlimit rl;
#endif
	//进程所能打开的最大文件描述符数
	nfiles = getdtablesize();

//设置资源限制的最大fd值
#ifdef RLIMIT_NOFILE
	if(getrlimit(RLIMIT_NOFILE, &rl) == 0)
	{
		nfiles = rl.rlim_cur;
		if( rl.rlim_max == RLIM_INFINITY )
			rl.rlim_cur = 8192;
		else
			rl.rlim_cur = rl.rlim_max;
		if( setrlimit( RLIMIT_NOFILE, &rl) == 0 )
			nfiles = rl.rlim_cur;
	}
#endif
//如果是SELECT不能超过FD_SETSIZE的值
	nfiles = MIN(nfiles, FD_SETSIZE);

	nselect = 0;

	return nfiles; 
}

void fdwatch_clear( void )
{
	maxfd = -1;
	conn_nums = 0;
	FD_ZERO( &master_wfdset );
	FD_ZERO( &master_rfdset );
}

void fdwatch_add_fd( int fd, int rw )
{
	conn_nums++;
	if(fd > maxfd)
		maxfd = fd;
	switch( rw )
	{
		case FD_READ:
			FD_SET(fd, &master_rfdset);
		case FD_WRITE:
			FD_SET(fd, &master_wfdset);
		default:
			return;
	}

}

int fdwatch( long timeout_msecs )
{
	return fdwatch_select( timeout_msecs );
}

static int fdwatch_select( long timeout_msecs )
{
	struct  timeval timeout;

	++nselect;
	working_rfdset = master_rfdset;
	working_wfdset = master_wfdset;
	if(timeout_msecs == INFTIM)
	{
		if((maxfd + 1) <= nfiles)
			return select(maxfd +1, &working_rfdset, &working_wfdset, NULL, (struct timeval*)0);
		else
		{
			perror("maxfd out of range");
			return -1;
		}
	}
	else
	{
		timeout.tv_sec = timeout_msecs / 1000L;
		timeout.tv_usec = timeout_msecs % 1000L * 1000L;
		if((maxfd + 1) <= nfiles)
			return select(maxfd + 1, &working_rfdset, &working_wfdset, NULL, &timeout);
		else
		{
			perror("maxfd out of range");
			return -1;
		}
	}	
}

int fdwatch_check_fd( int fd, int rw)
{
	switch( rw )
		{
			case FD_READ:
				return FD_ISSET(fd, &working_rfdset);
			case FD_WRITE:
				return FD_ISSET(fd, &working_wfdset);
			default:
				return 0;
		}
}

void fdwatch_status( long* nselectP )
{
	*nselectP = nselect;
	nselect = 0;
}

int fdwatch_get_conn_nums(void)
{
	return conn_nums;
}
