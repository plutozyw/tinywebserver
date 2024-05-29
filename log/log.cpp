#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        // 设置写入方式 flag
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size); // todo 有必要设计为模板类吗?
        pthread_t tid;
        // flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    // 输出内容长度
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    // 日志最大行数
    m_split_lines = split_lines;

    time_t t = time(NULL);             // 获取当前时间（自1970年1月1日以来的秒数）
    struct tm *sys_tm = localtime(&t); // 获取本地时间
    struct tm my_tm = *sys_tm;         // 复制本地时间

    // 从后往前找到第一个 / 的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0}; // 初始化日志文件名数组

    // 相当于自定义日志名
    // 若输入的文件名没有 /，则直接将时间 + 文件名作为日志名
    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        // 将 / 的位置向后移动一个位置，然后复制到 logname 中
        // p - file_name + 1 是文件所在路径文件夹的长度
        // dirname 相当于 ./
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0}; // 存储日志级别字符串
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    // 写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock(); // 加锁，保证线程安全
    m_count++;      // 更新现有行数

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) // everyday log
    {

        char new_log[256] = {0}; // 存储新的日志文件名
        fflush(m_fp);            // 刷新文件流
        fclose(m_fp);            // 关闭文件
        char tail[16] = {0};     // 存储时间部分的字符串

        // 格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 如果时间不是今天，就创建今天的日志，更新 m_today 和 m_count
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            // 超过最大行，在之前的日志名基础上，加后缀
            // m_count / m_split_lines
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a"); // 打开新的日志文件
    }

    m_mutex.unlock();

    va_list valst;           // 创建可变参数列表
    va_start(valst, format); // 初始化可变参数列表

    string log_str; // 存储日志内容的字符串
    m_mutex.lock();

    // 写入内容格式：时间 + 内容
    // 写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    // 内容格式化
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);// 异步写入日志队列
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp); // 同步写入日志文件
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    // 强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
