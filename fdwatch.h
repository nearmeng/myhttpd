#ifndef FDWATCH_H_
#define FDWATCH_H_

#define FD_READ 0
#define FD_WRITE 1

#ifndef INFTIM
#define INFTIM -1
#endif

//获得最大wathch数
extern int fdwatch_get_nfiles( void );

//清除fd_set
extern void fdwatch_clear(void);

//add fd to set
extern void fdwatch_add_fd(int fd, int rw);

//fdwatch loop
extern int fdwatch(long timeout_msecs);

//check the fd 
extern int fdwatch_check_fd(int fd, int rw);

extern void fdwatch_stats(long* nselectP);

extern int fdwatch_get_conn_nums(void);

#endif
