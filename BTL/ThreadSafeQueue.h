#pragma once
#include "Thread.h"
#include <cstdint>

template <typename T, uint16_t BufferSize = 100>
class BKE_ThreadSafeQueue{
	T buffer[BufferSize];
	BKE_Mutex mutex;
	BKE_ThreadSafeQueue(const BKE_ThreadSafeQueue &b){}
	BKE_ThreadSafeQueue &operator=(const BKE_ThreadSafeQueue &b)const{ return *this; }
	int _size;
	int _lpos;
	int limit(int _p)
	{
		if (_p < 0)
		{
			_p += BufferSize;
		}
		else if (_p >= BufferSize)
		{
			_p -= BufferSize;
		}
		return _p;
	}
	volatile bool *_flag;
public:
	BKE_ThreadSafeQueue(volatile bool *flag = NULL){
		_size = 0;
		_lpos = 0;
		_flag = flag;
	}
	
	void lock(){
		this->mutex.lock();
	}
	void unlock(){
		this->mutex.unlock();
	}
	void push(const T &e){
		while (1){
			this->mutex.lock();
			if (_size < BufferSize)
				break;
			if (_flag && *_flag)
			{
				this->mutex.unlock();
				return;
			}
			this->mutex.unlock();
			BKE_Thread::sleep(10);
		}
		int rpos = limit(_lpos + _size);
		buffer[rpos] = e;
		_size++;
		this->mutex.unlock();
	}
	bool is_empty(){
		BKE_MutexLocker ml(this->mutex);
		return !_size;
	}
	ulong size(){
		BKE_MutexLocker ml(this->mutex);
		return _size;
	}
	T &peek(){
		BKE_MutexLocker ml(this->mutex);
		return buffer[_lpos];
	}
	T pop(){
		BKE_MutexLocker ml(this->mutex);
		T ret = buffer[_lpos];
		_lpos = limit(_lpos + 1);
		_size--;
		return ret;
	}
	void pop_without_copy(){
		BKE_MutexLocker ml(this->mutex);
		_lpos = limit(_lpos + 1);
		_size--;
	}
};