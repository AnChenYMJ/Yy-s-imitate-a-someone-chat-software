#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "../utility/locker.h"

template<typename T>
class threadpool {
public:
	/*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的等待处理请求的数量*/
	threadpool(int thread_number = 8, int max_requests = 10000);
	~threadpool();
	/*往请求队列中添加任务*/
	bool append(T* request);
private:
	/*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
	static void* worker(void *arg);
	void run();
private:
	int m_thread_number; 	/*线程池中的线程数*/
	int m_max_request;		/*请求队列中允许的最大请求数*/
	pthread_t* m_threads;	/*描述线程池的数组，大小为m_thread_number*/
	std::list<T*> m_workqueue; /*请求队列*/
	locker m_queuelocker;	/*保护请求队列的互斥锁*/
	sem m_queuestat;		/*是否有任务需要处理*/
	bool m_stop;			/*是否结束线程*/
};

//构造函数，用了类模板，模板参数为conn对象		
//创建线程池
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
	m_thread_number(thread_number), m_max_request(max_requests),	//直接在函数名后面初始化参数， 注意在 ： 后的都是初始化
	m_stop(false), m_threads(NULL) {

	//参数检测
	if (thread_number <= 0 || max_requests <= 0) {
		//throw：抛出异常。异常必须显式地抛出，才能被检测和捕获到；如果没有显式的抛出，即使有异常也检测不到。
		throw std::exception();									
		//std::exception：标准异常类的基类，其类的声明在头文件<exception>中。所有标准库的异常类均继承于此类，因此通过引用类型可以捕获所有标准异常。
	}

	//pthread_create创建线程函数，参数1所需的类型：pthread_t，起到给线程做标识的作用
	//pthread_t 是一种用于表示线程的数据类型，每一个 pthread_t 类型的变量都可以表示一个线程。
	//线程数组，线程池
	m_threads = new pthread_t[m_thread_number];			
	if (!m_threads) {
		//抛出并获取异常
		throw std::exception();
	}

	/*创建thread_number个线程，并将它们都设置为脱离线程*/
	for (int i = 0; i < thread_number; i++) {
		printf("create the %dth thread\n", i);
		//pthread_create() 函数用来创建线程：（看笔记），参数3是线程创建后 最开始就执行的函数，参数4是参3线程函数的参数
		if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
			delete []m_threads;		//delete[] 释放new分配的 对象数组 指针指向的内存
			throw std::exception();	//抛出并获取异常
		}
		//pthread_detach（看笔记）	线程被置为detach状态（线程分离）线程一旦终止就立刻回收它占用的所有资源
		if (pthread_detach(m_threads[i])) {
			delete []m_threads;
			throw std::exception();
		}
	}
}

template <typename T>
threadpool<T>::~threadpool() {
	delete []m_threads;
	m_stop = true;
}

//此函数在主函数main中的循环里，根据读到的数据来决定是否调用
//
template<typename T>
bool threadpool<T>::append(T* request) {
	/*操作工作队列时一定要加锁，因为它被所有线程共享*/
	m_queuelocker.lock();

	if (m_workqueue.size() > m_max_request) {
		m_queuelocker.unlock();
		return false;
	}
	
	//push_back函数将一个新的元素加到List容器的最后面
	m_workqueue.push_back(request);		//向m_workqueue容器（工作队列）添加conn对象request 的地址
	m_queuelocker.unlock();				//释放锁
	m_queuestat.post();					//发信号，这个资源能使用了
	return true;
}

//线程函数：线程创建后，最开始执行的函数
template <typename T>
void* threadpool<T>::worker(void *arg) {	//这里传入的是this，就是本对象的指针
	threadpool* pool = (threadpool *)arg;	//本对象指针强转为(threadpool *)
	pool->run();							//调用run函数
	return pool;							//返回强转后的本对象指针pool
}

//线程函数中调用
template<typename T> 
void threadpool<T>::run() {
	//循环执行，直到m_stop == 1	m_stop决定着是否结束线程(对象初始化时赋值了false<构造函数内>)
	//循环等待main中append调用，给容器添加元素（因为在工作线程中，所以循环不阻碍主线程运行）
	while (!m_stop) {
		//m_queuestat决定 是否有任务需要处理  
		m_queuestat.wait();					//sem_wait取获取信号灯，信号量相关，参考：https://blog.csdn.net/lh2016rocky/article/details/70800958
		//m_queuelocker：保护请求队列的互斥锁
		m_queuelocker.lock();				//获取互斥锁
		//如果m_workqueue容器（工作队列）为空（没有内容），则释放互斥锁
		if (m_workqueue.empty()) {			//m_workqueue：请求队列，list容器，empty检查列表容器是否为空
			m_queuelocker.unlock();			//释放互斥锁
			continue;						//返回循环开头
		}

		//将工作队列中的conn对象（每个客户端连接后都创建）赋值给此处的request //T为conn类
		T* request = m_workqueue.front();	// front此函数可用于获取列表的第一个元素。在append中将conn对象地址存进了容器
		m_workqueue.pop_front();			//pop_front()该函数删除列表容器的第一个元素，意味着该容器的第二个元素成为第一个元素，并且该容器中的第一个元素从该容器中删除。
		m_queuelocker.unlock();				//释放互斥锁
		if (!request) {
			continue;
		}
		request->process();					//conn对象调用process函数（每个客户端连接后都创建了一个conn对象，所以每个客户端都会执行这个）
	}
}

#endif