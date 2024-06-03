#ifndef MYTHREADPOOL_H
#define MYTHREADPOOL_H

#include <list>
#include <queue>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class ThreadPool
{
public:
    // 构造函数(线程数，最大请求数)
    ThreadPool(int threadNum = 8, int maxRequests = 10000);
    // 析构函数
    ~ThreadPool();
    // 插入任务队列函数
    void append(T *req);

private:
    // 工作线程运行的函数
    static void *worker(void *arg); // done 为什么是私有属性？不是给外部调用的接口

    void run(); // todo 不要他行不行?有了之后可以方便使用成员变量,静态函数没有this指针，不方便调用内部成员

    // 线程池中的线程数
    int m_threadNum;
    // 任务队列最大请求数
    int m_maxRequests;
    // 指向线程池数组的指针
    pthread_t *m_threads;
    // 任务队列
    std::queue<T *> m_taskQ; // done 用vector还是list？应该用list，因为需要频繁的插入和删除
    // todo 用queue和list和deque有什么区别，哪个更好？
    // todo 为什么要用T* 不用T？
    // 互斥锁
    locker m_mutex; // todo 不封装该怎么写？
    // 信号量
    sem m_sem; // todo 用条件变量还是信号量？
};

#endif

template <typename T>
ThreadPool<T>::ThreadPool(int threadNum, int maxRequests)
{
    // 创建多个线程、线程分离
    assert(threadNum >= 0);
    assert(maxRequests >= 0);
    m_threads = new pthread_t[threadNum];
    if (!m_threads)
    {
        // todo 出错怎么处理？
    }
    for (int i = 0; i < threadNum; i++)
    {
        // 参数：指针，NULL，运行函数，参数
        pthread_create(m_threads + i, NULL, worker, this);

        pthread_detach(m_threads[i]);
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
}

template <typename T>
void ThreadPool<T>::append(T *req)
{
    // 插入任务
    // 信号量+1
    m_mutex.lock();
    if (m_taskQ.size() >= m_maxRequests)
    {
        m_mutex.unlock();
        return; // todo 如果超过了，应该什么处理逻辑？
    }
    m_taskQ.push(req);
    m_mutex.unlock();
    m_sem.post();
}

template <typename T>
void *ThreadPool<T>::worker(void *arg)
{
    ThreadPool* pool=(ThreadPool*)arg;
    pool->run();
    return nullptr;
}

template <typename T>
void ThreadPool<T>::run()
{
    //从任务队列取出任务执行。
    while (1)
    {
        m_sem.wait();
        m_mutex.lock();
        T* req=m_taskQ.front();
        if(!req)
        {
            m_mutex.unlock();
            continue;
        }
        m_taskQ.pop();
        m_mutex.unlock();

        req->process();
    }
    
}
