// 游双书中源代码
#ifndef tIME_HEAP
#define tIME_HEAP

#include <iostream>
#include <netinet/in.h>
#include <time.h>
#include <unordered_map>
#include <vector>
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

    heap_timer();

public:
    time_t expire;                  // 定时器生效绝对时间
    void (*cb_func)(client_data *); // 定时器回调函数
    client_data *user_data;         // 用户数据
};

/*时间堆类(最小堆、二叉堆、小根堆)*/
class time_heap
{
public:
    time_heap();

    ~time_heap();

public:
    // todo 没有adjust_timer?
    void adjust_timer(heap_timer *timer);

    // 添加定时器
    void add_timer(heap_timer *timer);

    // 删除定时器
    void del_timer(heap_timer *timer);

    void tick();

    // 返回栈顶元素还有多久超时
    int getOverTime();

private:
    // 删除指定位置的定时器
    void del(int i);
    // 下滤，确保以第i个节点为根的子树拥有最小堆性质
    void percolate_down(int i);

    // 交换节点
    void swapNode(int i, int j);

    // 上滤，确保以第i个节点为根的子树拥有最小堆性质
    void percolate_up(int i);

private:
    // 数组模拟堆
    std::vector<heap_timer> array;

    // 存sockid和堆索引对应关系
    std::unordered_map<int, int> ref;
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
    time_heap m_time_heap;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
