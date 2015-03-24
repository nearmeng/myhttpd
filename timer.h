#ifndef _TIMER_H_
#define _TIMER_H_

#include <stdio.h>
#include <sys/time.h>

#ifndef INFTIM
#define INFTIM -1
#endif

//传入timeout函数的参数
typedef union
{
	void* p;
	int i;
	long int l;
}timeout_args;

typedef void timeout_func(timeout_args args, struct timeval* now);

//定义timer struct
typedef struct timer_struct
{
	timeout_func* timer_proc;
	timeout_args args;
	struct timeval time;
	long msecs;
	int periodic;

	struct timer_struct* next;
} Timer;

//模块对外函数

//创建一个定时器
extern Timer* tmr_create(timeout_func* timer_proc, timeout_args args, struct timeval* now, long msecs, int periodic);

//运行一个定时器
extern void tmr_run(struct timeval* now);

//查看最近的定时器触发时间-毫秒
extern long tmr_timeout_ms(struct timeval* now);

//查看最近的定时器触发时间-struct timeval
extern struct timeval* tmr_timeout(struct timeval* now);

//重置一个定时器
extern void tmr_reset(Timer* timer, struct timeval* now);

//取消一个定时器
extern void tmr_cancle(Timer* timer);

//清除定时器结构
extern void tmr_clean(void);

//销毁所以定时器内存
extern void tmr_destroy(void);

#endif
