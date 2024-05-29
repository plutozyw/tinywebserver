#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    // C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance() // 获取Log类的唯一实例
    {
        static Log instance; // 静态局部变量，保证线程安全的懒汉单例模式
        return &instance;
    }

    // 异步写日志公有方法，调用私有方法 async_write_log
    static void *flush_log_thread(void *args)
    {
        // Log类的唯一实例指针
        // 类内访问静态成员，也需要声明作用域 ::
        Log::get_instance()->async_write_log(); // 调用日志实例的异步写日志方法
    }
    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    // 将输出内容按照标准格式整理
    // ... 表示 可变参数列表
    void write_log(int level, const char *format, ...); // 写日志方法  //todo 是同步还是异步写?

    // 强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log(); // 虚析构函数 //TODO 为什么是虚函数?
    // 异步写日志方法
    void *async_write_log()
    {
        string single_log; // 单条日志字符串
        // 从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log)) // todo 如果pop失败呢?
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp); // 将日志内容写入文件
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128];               // 路径名
    char log_name[128];               // log文件名
    int m_split_lines;                // 日志最大行数
    int m_log_buf_size;               // 日志缓冲区大小
    long long m_count;                // 日志行数记录
    int m_today;                      // 因为按天分类,记录当前时间是那一天
    FILE *m_fp;                       // 打开log的文件指针
    char *m_buf;                      // 要输出的内容
    block_queue<string> *m_log_queue; // 阻塞队列
    bool m_is_async;                  // 是否同步标志位
    locker m_mutex;                   // 同步类
    int m_close_log;                  // 关闭日志
};

// 可变参数宏定义
// 以下，4 个宏定义在其他文件使用，主要用于不同类型的日志输出
//日志类中的方法都不会被其他程序直接调用，末尾的四个可变参数宏，提供了其他程序的调用方法  //todo 为什么不让其他程序直接调用

// 调试日志输出宏
#define LOG_DEBUG(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_INFO(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_WARN(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_ERROR(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }

#endif
