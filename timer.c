#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "timer.h"


static Timer* active_timers = NULL;
static Timer* free_timers = NULL;

static void set_timeout_time(Timer* timer)
{
	timer->time.tv_sec += (timer->msecs / 1000);
	timer->time.tv_usec += (timer->msecs % 1000) * 1000;
	if(timer->time.tv_usec > 10e6)
	{
		timer->time.tv_sec += (timer->time.tv_usec / 10e6); 
		timer->time.tv_usec %= 1000000;
	}
}

extern Timer* tmr_create(timeout_func* timer_proc, timeout_args args, struct timeval* now, long msecs, int periodic)
{

	Timer* timer;

	if(free_timers == NULL)
	{
		timer = (Timer*)malloc(sizeof(Timer));
		if(timer == NULL)
		{
			return NULL;
		}
	}
	else
	{
		timer = free_timers;
		free_timers = free_timers->next;
	}

	if(timer != NULL)
	{
		timer->timer_proc = timer_proc;
		timer->args = args;
		timer->msecs = msecs;
		timer->periodic = periodic;
		if(now == NULL)
		{
			gettimeofday(&timer->time, NULL);
		}
		else
		{
			timer->time = *now;
		}
		//timer只的是超时绝对时间
		set_timeout_time(timer);
	}
	//插入链表
	timer->next = active_timers;
	active_timers = timer;

	return timer;
}

extern void tmr_run(struct timeval* now)
{
	Timer* t;

	for(t = active_timers; t != NULL; t = t->next)
	{
		if((now->tv_sec > t->time.tv_sec) || (now->tv_sec == t->time.tv_sec && 
			now->tv_usec >= t->time.tv_usec))
		{
			//运行timeout函数
			(*t->timer_proc)(t->args, now);
			if(t->periodic)
			{
				set_timeout_time(t);
			}
			else
			{
				tmr_cancle(t);
			}
		}
	}
}

extern long tmr_timeout_ms(struct timeval* now)
{
	Timer* t;
	long lest = 0;
	long this;

	if(active_timers == NULL)
	{
		return -1;
	}

	for(t = active_timers; t != NULL; t = t->next)
	{
		this = (t->time.tv_sec - now->tv_sec) * 1000 + (t->time.tv_usec - now->tv_sec) / 1000;
		if( t == active_timers)
		{
			lest = this;
		}
		else if(this < lest)
		{
			lest = this;
		}
	}

	if(lest < 0)
		lest = 0;

	return lest;
}

extern struct timeval* tmr_timeout(struct timeval* now)
{
	long msecs;
	static struct timeval time;

	msecs = tmr_timeout_ms(now);
	if(msecs == -1)
		return NULL;
	else
	{
		time.tv_sec = msecs / 1000;
		time.tv_usec = (msecs % 1000) * 1000;
	}

	return &time;
}

//重置一个定时器
extern void tmr_reset(Timer* timer, struct timeval* now)
{
	timer->time = *now;
	set_timeout_time(timer);
}

//取消一个定时器
extern void tmr_cancle(Timer* timer)
{
	Timer** t;

	for(t = &active_timers; *t != NULL; t = &((*t)->next))
	{
		if(*t == timer)
		{
			*t = (*t)->next;
			timer->next = free_timers;
			free_timers = timer;
			return;
		}
	}
}

//清除无用的定时器结构
extern void tmr_clean(void)
{
	Timer* t;

	while(free_timers != NULL)
	{
		t = free_timers;
		free_timers = free_timers->next;
		free((void*)t);
	}
}

//销毁所有定时器内存
extern void tmr_destroy(void)
{
	Timer* t;

	while(active_timers != NULL)
		tmr_cancle(active_timers);

	tmr_clean();
}
