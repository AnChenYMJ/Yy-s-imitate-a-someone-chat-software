#include <iostream>
#include <exception>
#include <pthread.h>
#include <semaphore.h>

#ifndef LOCKER_H
#define LOCKER_H

//信号量与互斥锁、条件变量
class sem {
private:
	sem_t m_sem;
public:
	//构造函数
	sem() {										//初始化（initialize），也叫做建立（create） int sem_init	建立信号量
		if (sem_init(&m_sem, 0, 0) != 0) {
			//抛出异常	异常为std::exception()创建
			throw::std::exception();			//throw：抛出异常  std::exception：标准异常类的基类，其类的声明在头文件<exception>中。
		}
	}
	sem(int num)//num为资源数
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
	
	~sem() {
		sem_destroy(&m_sem);
	}

	bool wait() {
		//sem_wait取获取信号灯
		return sem_wait(&m_sem) == 0;	//信号量：参考：https://blog.csdn.net/lh2016rocky/article/details/70800958
	} 

	bool post() {
		return sem_post(&m_sem) == 0;	//给信号（signal）或发信号（post） int sem_post（sem_t *sem）;
	}
};

class locker {
public:
	//创建并销毁互斥锁
	locker() {
		//该函数用于C函数的多线程编程中，互斥锁的初始化。	//m_mutex为互斥变量，理解为互斥锁的标识
		if (pthread_mutex_init(&m_mutex, NULL) != 0) {	//如果参数attr为空(NULL)，则使用默认的互斥锁属性，默认属性为快速互斥锁 。
			throw std::exception();
		}
	}
	
	//销毁互斥锁
	~locker() {
		//功能为互斥锁销毁函数在执行成功后返回 0，否则返回错误码。
		pthread_mutex_destroy(&m_mutex);			
	}

	//获取互斥锁
	bool lock() {
		return pthread_mutex_lock(&m_mutex) == 0;
	}

	//释放互斥锁
	bool unlock() {
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
private:
	//互斥变量使用特定的数据类型：pthread_mutex_t
	pthread_mutex_t m_mutex;
};


class cond {

public:
	//创建并初始化条件变量								
	cond() {
		if (pthread_mutex_init(&m_mutex, NULL) !=0 ) {
			throw std::exception();
		}
		if (pthread_cond_init(&m_cond, NULL) != 0) {
			//构造函数出现问题，释放已经分配的资源
			pthread_mutex_destroy(&m_mutex);
			throw std::exception();
		}
	}

	//销毁条件变量
	~cond() {
		pthread_mutex_destroy(&m_mutex);
		pthread_cond_destroy(&m_cond);
	}

	//等待条件变量
	bool wait() {
		int ret = 0;
		pthread_mutex_lock(&m_mutex);
		ret = pthread_cond_wait(&m_cond, &m_mutex);
		pthread_mutex_unlock(&m_mutex);
		return ret == 0;
	}

	//唤醒等待条件变量的线程
	bool signal() {
		return pthread_cond_signal(&m_cond) == 0;
	}
private:
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
};


// class LockMap {
// 	LockMap()

// private:
// 	unordered_map<int, int> mp; 	//userId -> sockfd
// 	locker lock;
// };

#endif