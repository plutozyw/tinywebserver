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
    // 构造函数
    ThreadPool();
    // 析构函数
    ~ThreadPool();
    // 插入任务队列函数
    void append();

private:
    // 工作线程运行的函数
    static void *worker(void *arg);//todo 为什么是私有属性？

    void run();//todo 不要他行不行?

    //线程池中的线程数
    int m_threadNumber;
    //任务队列最大请求数
    int m_maxRequests;
    //指向线程池数组的指针
    pthread_t* m_threads;
    //任务队列
    std::queue<T *> m_taskQ; //done 用vector还是list？应该用list，因为需要频繁的插入和删除
    //todo 用queue和list和deque有什么区别，哪个更好？
    //todo 为什么要用T* 不用T？
    //互斥锁
    locker m_mutex;//todo 不封装该怎么写？
    //信号量
    sem m_sem;//todo 用条件变量还是信号量？
};

#endif