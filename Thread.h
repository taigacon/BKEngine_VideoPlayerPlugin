#ifndef BKE__THREAD_H
#define BKE__THREAD_H

#include "Common.h"
#include "Binder.h"
#ifndef DONT_USE_COCOS2D
#include "typedefs.h"
#endif
#include <vector>

typedef void (*BKE_ThreadedFunctionPointer)(void *);

#if BKE_SYS_WINDOWS
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <Windows.h>
typedef HANDLE BKE_Thread_internal;
typedef DWORD BKE_ThreadID;
#define BKE_Thread_DECLARE_THREAD_FUNCTION(name) unsigned long __stdcall name(void *p)
#elif BKE_SYS_UNIX
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
typedef pthread_t BKE_Thread_internal;
typedef pthread_t BKE_ThreadID;
#define BKE_Thread_DECLARE_THREAD_FUNCTION(name) void *name(void *p)
#else
#error 请设定平台！
#endif

class  BKE_Thread{
	struct threadStruct{ BKE_ThreadedFunctionPointer f; void *d; };
	BKE_Thread_internal thread;
	bool called,
		join_at_destruct;
	void free();
public:
	static BKE_Thread_DECLARE_THREAD_FUNCTION(runningThread);
	BKE_Thread():called(0),join_at_destruct(1){}
	BKE_Thread(BKE_ThreadedFunctionPointer function,bool give_highest_priority=0):called(0),join_at_destruct(1){
		this->call(function,0,give_highest_priority);
	}
	template <typename T>
	BKE_Thread(T *o,bool give_highest_priority=0):called(0),join_at_destruct(1){
		this->call(o,give_highest_priority);
	}
	~BKE_Thread();
	void call(BKE_ThreadedFunctionPointer function,void *data,bool give_highest_priority=0);
	template <typename T>
	void call(T *o,bool give_highest_priority=0){
		this->call(&call_bound<T>,o,give_highest_priority);
	}
	void join();
	void unbind();
	static inline BKE_ThreadID GetThreadId(){
#if BKE_SYS_WINDOWS
		return GetCurrentThreadId();
#elif BKE_SYS_UNIX
		return pthread_self();
#endif
	}
#if BKE_SYS_WINDOWS
	static void sleep(DWORD ms){
		Sleep(ms);
	}
#elif BKE_SYS_UNIX
	static void sleep(int ms){
		usleep(ms);
	}
#endif
};

class BKE_Notify{
	bool initialized;
#if BKE_SYS_WINDOWS
	HANDLE event;
#elif BKE_SYS_UNIX
	sem_t sem;
#endif
public:
	BKE_Notify():initialized(0){}
	void init();
	~BKE_Notify();
	void set();
	void reset();
	void wait();
};

class BKE_Mutex{
#if BKE_SYS_WINDOWS
	//pointer to CRITICAL_SECTION
	void *mutex;
#elif BKE_SYS_UNIX
	pthread_mutex_t mutex;
#endif
public:
	BKE_Mutex();
	~BKE_Mutex();
	void lock();
	void unlock();
};

class BKE_MutexLocker{
	BKE_Mutex &mutex;
	BKE_MutexLocker(const BKE_MutexLocker &m):mutex(m.mutex){}
	void operator=(const BKE_MutexLocker &){}
public:
	BKE_MutexLocker(BKE_Mutex &m):mutex(m){
		this->mutex.lock();
	}
	~BKE_MutexLocker(){
		this->mutex.unlock();
	}
};


class BKE_Clock{
#if BKE_SYS_WINDOWS
	void *data;
#endif
public:
	typedef double t;
	static t Max;
#if BKE_SYS_WINDOWS
	BKE_Clock();
	~BKE_Clock();
#endif
	t get() const;
};


#endif
