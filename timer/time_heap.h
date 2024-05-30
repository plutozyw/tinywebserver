// 游双书中源代码
#ifndef tIME_HEAP
#define tIME_HEAP

#include <iostream>
#include <netinet/in.h>
#include <time.h>
using std::exception;

#define BUFFER_SIZE 64

class heap_timer;
/*绑定socket和定时器*/
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer *timer;
};

/*定时器类(一个节点)*/
class heap_timer
{
public:
    heap_timer(int delay);
    
public:
    time_t expire;                  // 定时器生效绝对时间
    void (*cb_func)(client_data *); // 定时器回调函数
    client_data *user_data;         // 用户数据
};

/*时间堆类*/
class time_heap
{
public:
    // 初始化大小为cap的空堆
    time_heap(int cap);

    // 用已有数组初始化堆
    time_heap(heap_timer **init_array, int size, int capacity);

    ~time_heap();

public:
    //todo 没有adjust_timer?
    void adjust_timer(heap_timer *timer);
    // 添加定时器
    void add_timer(heap_timer *timer);

    // 删除定时器
    void del_timer(heap_timer *timer);
    
    // 获取堆顶部的定时器
    heap_timer *top() const;
    
    // 删除堆顶部的定时器
    void pop_timer();
    
    void tick();
    
    bool empty() const;

private:
    // 下滤，确保以第hole个节点为根的子树拥有最小堆性质
    void percolate_down(int hole);

    // 堆数组容量扩大一倍
    void resize() throw(std::exception);
    
private:
    heap_timer **array; // 堆数组
    int capacity;       // 容量
    int cur_size;       // 当前元素个数
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};


#endif
