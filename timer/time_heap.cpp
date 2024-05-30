#include "time_heap.h"
#include "../http/http_conn.h"

heap_timer::heap_timer(int delay)
{
    expire = time(NULL) + delay;
}

heap_timer::heap_timer()
{
}

time_heap::time_heap()
{
    array.reserve(64);
}

time_heap::~time_heap()
{
    ref.clear();
    array.clear();
}

// 调整定时器
void time_heap::adjust_timer(heap_timer *timer)
{
    int id = timer->user_data->sockfd;
    assert(!array.empty() && ref.count(id));
    // if ( !ref.count(id))
    // {
    //     printf("ref.count(%d) error\n",id);
    //     return;
    // }
    
    // 下滤
    percolate_down(ref[id]);
    percolate_up(ref[id]);
}

// 添加定时器
void time_heap::add_timer(heap_timer *timer)
{
    int id = timer->user_data->sockfd;
    assert(id >= 0);
    if (!timer)
    {
        return;
    }
    if (!ref.count(id))
    {
        int i = array.size();
        ref[id] = i;
        // printf("add ref[%d]=%d\n",id,i);
        array.push_back(*timer);
        percolate_up(i);
    }
    else
    {
        int i = ref[id];
        array[i] = *timer;
        // printf("add ref[%d]=%d again \n",id,i);
        percolate_down(i);
        percolate_up(i);
    }
}

// 删除定时器
void time_heap::del_timer(heap_timer *timer)
{
    if (!timer)
    {
        return;
    }
    int id=timer->user_data->sockfd;

    int i=ref[id];

    del(i);
}

void time_heap::tick()
{
    if (array.empty())
        return;

    time_t cur = time(NULL);
    while (!array.empty())
    {
        heap_timer tmp = array.front();

        // 如果堆顶定时器没有到期，退出循环
        if (tmp.expire > cur)
        {
            break;
        }
        // 否则就执行堆顶定时器中的任务
        tmp.cb_func(tmp.user_data);

        // 将堆顶元素删除，同时生成新的堆顶定时器
        del(0);
    }
}

void time_heap::del(int i)
{
    assert(!array.empty() && i >= 0 && i < array.size());

    int n = array.size() - 1;
    if(i==n)//要删除最后一个元素，直接删
    {
        // printf("delete ref[%d]=%d n=%d\n",array.back().user_data->sockfd,ref[array.back().user_data->sockfd],n);
        ref.erase(array.back().user_data->sockfd);
        array.pop_back();
        return;
    }
    swapNode(i, n);

    // printf("delete ref[%d]=%d n=%d\n",array.back().user_data->sockfd,ref[array.back().user_data->sockfd],n);

    ref.erase(array.back().user_data->sockfd);
    array.pop_back();
    
    // 如果堆空就不用调整了
    if (!array.empty())
    {
        percolate_down(i);
        percolate_up(i);
    }
}

// 下滤，确保以第i个节点为根的子树拥有最小堆性质
void time_heap::percolate_down(int i)
{
    int s = array.size();
    assert(i >= 0 && i < s);
    int t = i * 2 + 1;

    while (t < s)
    {
        if (t + 1 < s && array[t + 1].expire < array[t].expire)
            t++;
        if (array[i].expire < array[t].expire)
            break;
        // swap(array[i], array[t]);
        swapNode(i, t);
        i = t, t = i * 2 + 1;
    }
}

void time_heap::swapNode(int i, int j)
{
    if (i == j)
        return;
    int s = array.size();
    assert(i >= 0 && i < s);
    assert(j >= 0 && j < s);
    std::swap(array[i], array[j]);
    ref[array[i].user_data->sockfd] = i;
    ref[array[j].user_data->sockfd] = j;
}

// 上滤，确保以第i个节点为根的子树拥有最小堆性质
void time_heap::percolate_up(int i)
{
    assert(i >= 0 && i < array.size());

    int j = (i - 1) / 2;
    while (j >= 0 && array[i].expire < array[j].expire)
    {
        swapNode(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    //  可重入性：中断后，再次进入该函数，环境变量与之前相同
    //  不会丢失数据
    int save_errno = errno;
    int msg = sig;
    // 信号值从 管道写端 写入，传输字符类型，而非 整型
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) // done 为什么不是声明为void(*handler)(int)?两种方法都可以
{
    // 创建 sigaction 结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    // 信号处理函数中仅发送 信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    // 对结构体变量 按位或，SA_RESTART 标志位变1
    // 表示需要自动重启
    if (restart) // TODO 自动重启的作用?
        sa.sa_flags |= SA_RESTART;
    // 所有信号添加到 信号集 中,会阻塞所有其他信号
    sigfillset(&sa.sa_mask);
    // 执行 sigaction() 函数
    assert(sigaction(sig, &sa, NULL) != -1); // 对信号设置新的处理方式
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_time_heap.tick(); // 处理链表上到期的定时器
    // m_timer_lst.tick(); // 处理链表上到期的定时器
    alarm(m_TIMESLOT); // 重新定时以不断触发 SIGALRM 信号
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    // 删除 非活动连接 在 socket 上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data); // 是一个宏，用于在代码中插入一条条件判断语句，如果条件为假，则终止程序运行并打印错误信息

    // 关闭文件描述符
    close(user_data->sockfd);

    // printf("timeout close %d\n", user_data->sockfd);

    // printf("http connect count %d\n", http_conn::m_user_count);

    // 减少连接数
    http_conn::m_user_count--;
}