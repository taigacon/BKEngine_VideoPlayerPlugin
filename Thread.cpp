#include <ctime>
#include <cstdlib>

#include "Thread.h"
#include <float.h>

#if BKE_SYS_WINDOWS
#include <windows.h>
#include <Aclapi.h>
#elif BKE_SYS_UNIX
#include <unistd.h>
#include <cerrno>
#endif



void BKE_Notify::init(){
#if BKE_SYS_WINDOWS
		this->event=CreateEvent(0,0,0,0);
#elif BKE_SYS_UNIX
		sem_init(&this->sem,0,0);
#endif
		this->initialized=1;
}

BKE_Notify::~BKE_Notify(){
	if (!this->initialized)
		return;
#if BKE_SYS_WINDOWS
	CloseHandle(this->event);
#elif BKE_SYS_UNIX
	sem_destroy(&this->sem);
#endif
}

void BKE_Notify::set(){
#if BKE_SYS_WINDOWS
	SetEvent(this->event);
#elif BKE_SYS_UNIX
	sem_post(&this->sem);
#endif
}

void BKE_Notify::reset(){
#if BKE_SYS_WINDOWS
	ResetEvent(this->event);
#endif
}

void BKE_Notify::wait(){
#if BKE_SYS_WINDOWS
	WaitForSingleObject(this->event,INFINITE);
#elif BKE_SYS_UNIX
	while (sem_wait(&this->sem)<0 && errno==EINTR){BKE_Thread::sleep(1);}
#endif
}

BKE_Thread::~BKE_Thread(){
	if (!this->called)
		return;
	if (this->join_at_destruct){
		this->join();
		this->free();
	}
}

void BKE_Thread::free(){
#if BKE_SYS_WINDOWS
	CloseHandle(this->thread);
#endif
}

BKE_Thread_DECLARE_THREAD_FUNCTION(BKE_Thread::runningThread){
	srand((unsigned int)time(0));
	BKE_ThreadedFunctionPointer f=((threadStruct *)p)->f;
	void *d=((threadStruct *)p)->d;
	delete (threadStruct *)p;
	f(d);
	return 0;
}

void BKE_Thread::call(BKE_ThreadedFunctionPointer function,void *data,bool give_highest_priority){
	if (this->called)
		return;
	threadStruct *ts=new threadStruct;
	ts->f=function;
	ts->d=data;
#if BKE_SYS_WINDOWS
	if (!give_highest_priority)
		this->thread=CreateThread(0,0,(LPTHREAD_START_ROUTINE)runningThread,ts,0,0);
	else{
		this->thread=CreateThread(0,0,(LPTHREAD_START_ROUTINE)runningThread,ts,REALTIME_PRIORITY_CLASS,0);
		SetThreadPriority(this->thread,THREAD_PRIORITY_TIME_CRITICAL);
	}
#elif BKE_SYS_UNIX
	pthread_attr_t attr,
		*pattr=0;
	if (give_highest_priority){
		pattr=&attr;
		pthread_attr_init(pattr);
		sched_param params;
		pthread_attr_getschedparam(pattr,&params);
		int policy;
		pthread_attr_getschedpolicy(pattr,&policy);
		params.sched_priority=sched_get_priority_max(policy);
		pthread_attr_setschedparam(pattr,&params);
	}
	pthread_create(&this->thread,pattr,runningThread,ts);
	if (give_highest_priority)
		pthread_attr_destroy(pattr);
#endif
	this->called=1;
}

void BKE_Thread::join(){
	if (!this->called)
		return;
#if BKE_SYS_WINDOWS
	WaitForSingleObject(this->thread, INFINITE);
#elif BKE_SYS_UNIX
	pthread_join(this->thread, NULL);
#endif
	this->called=0;
}

void BKE_Thread::unbind(){
	if (!this->join_at_destruct)
		return;
	this->join_at_destruct=0;
	this->free();
}



BKE_Mutex::BKE_Mutex(){
#if BKE_SYS_WINDOWS
	this->mutex=new CRITICAL_SECTION;
	InitializeCriticalSection((CRITICAL_SECTION *)this->mutex);
#elif BKE_SYS_UNIX
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&this->mutex,&attr);
	pthread_mutexattr_destroy(&attr);
#endif
}

BKE_Mutex::~BKE_Mutex(){
#if BKE_SYS_WINDOWS
	DeleteCriticalSection((CRITICAL_SECTION *)this->mutex);
	delete (CRITICAL_SECTION *)this->mutex;
#elif BKE_SYS_UNIX
	pthread_mutex_destroy(&this->mutex);
#endif
}

void BKE_Mutex::lock(){
#if BKE_SYS_WINDOWS
	EnterCriticalSection((CRITICAL_SECTION *)this->mutex);
#elif BKE_SYS_UNIX
	pthread_mutex_lock(&this->mutex);
#endif
}

void BKE_Mutex::unlock(){
#if BKE_SYS_WINDOWS
	LeaveCriticalSection((CRITICAL_SECTION *)this->mutex);
#elif BKE_SYS_UNIX
	pthread_mutex_unlock(&this->mutex);
#endif
}



#undef MAX

BKE_Clock::t BKE_Clock::Max = DBL_MAX;

#if BKE_SYS_WINDOWS
BKE_Clock::BKE_Clock(){
	Uint64 *p = new Uint64;
	LARGE_INTEGER li;
	*p = (!QueryPerformanceFrequency(&li)) ? 0 : li.QuadPart / 1000;
	this->data = p;
}

BKE_Clock::~BKE_Clock(){
	delete (Uint64 *)this->data;
}
#endif

BKE_Clock::t BKE_Clock::get() const{
#if BKE_SYS_WINDOWS
	const Uint64 *p = (const Uint64 *)this->data;
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return BKE_Clock::t(li.QuadPart) / BKE_Clock::t(*p);
#elif BKE_SYS_UNIX
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return BKE_Clock::t(tv.tv_sec)*1000.0 + BKE_Clock::t(tv.tv_usec) / 1000.0;
#endif
}