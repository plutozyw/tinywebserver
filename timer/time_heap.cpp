#include "time_heap.h"
#include "../http/http_conn.h"

heap_timer::heap_timer(int delay)
{
    expire = time(NULL) + delay;
}

heap_timer::heap_timer()
{
}

// 初始化大小为cap的空堆
time_heap::time_heap(int cap) throw(std::exception) : capacity(cap), cur_size(0)
{
    array = new heap_timer *[capacity]; // 指针数组
    if (!array)
    {
        throw std::exception();
    }
    for (int i = 0; i < capacity; ++i)
    {
        array[i] = NULL;
    }
}

// 用已有数组初始化堆
time_heap::time_heap(heap_timer **init_array, int size, int capacity) throw(std::exception) : cur_size(size), capacity(capacity)
{
    if (capacity < size)
    {
        throw std::exception();
    }
    array = new heap_timer *[capacity];
    if (!array)
    {
        throw std::exception();
    }
    for (int i = 0; i < capacity; ++i)
    {
        array[i] = NULL;
    }
    if (size != 0)
    {
        for (int i = 0; i < size; ++i)
        {
            array[i] = init_array[i];
        }
        for (int i = (cur_size - 1) / 2; i >= 0; --i)
        {
            percolate_down(i); // 下滤
        }
    }
}

time_heap::~time_heap()
{
    for (int i = 0; i < cur_size; ++i)
    {
        delete array[i]; // 释放了由array[i]指向的对象所占用的内存
    }
    delete[] array; // 释放了由array指向的整个数组所占用的内存
}

//调整定时器
void time_heap::adjust_timer(heap_timer *timer)
{
    //umap存sockid和堆索引对应关系

    //下滤
    //percolate_down();

}

// 添加定时器
void time_heap::add_timer(heap_timer *timer) throw(std::exception)
{
    if (!timer)
    {
        return;
    }
    if (cur_size >= capacity) // 容量不够，扩大一倍
    {
        resize();
    }
    // hole是新建空穴位置；
    int hole = cur_size++; // 在最后的位置创建空穴，把增加的节点放在空穴处
    int parent = 0;
    // 从空穴到根节点的路径上所有节点执行上滤
    for (; hole > 0; hole = parent)
    {
        parent = (hole - 1) / 2;                    // 父节点位置
        if (array[parent]->expire <= timer->expire) // 父节点比新增节点小
        {
            break; // 新增节点可以放在此空穴处，上滤完成
        }
        array[hole] = array[parent]; // 否则，交换空穴它父节点
    }
    array[hole] = timer;
}

// 删除定时器
void time_heap::del_timer(heap_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // lazy delelte
    // 仅仅将目标定时器的回调函数设置为空，节省真正删除定时器造成的开销，但是容易使堆数组膨胀
    timer->cb_func = NULL; // todo lazy delete有什么影响?
}

// 获取堆顶部的定时器
heap_timer *time_heap::top() const
{
    if (empty())
    {
        return NULL;
    }
    return array[0];
}

// 删除堆顶部的定时器
void time_heap::pop_timer()
{
    if (empty())
    {
        return;
    }
    if (array[0])
    {
        delete array[0];
        // 将原来的堆顶元素替换为数组最后一个元素
        array[0] = array[--cur_size];
        percolate_down(0); // 对新的堆顶元素执行下滤操作
    }
}

void time_heap::tick()
{
    heap_timer *tmp = array[0];
    time_t cur = time(NULL);
    while (!empty())
    {
        if (!tmp)
        {
            break;
        }
        // 如果堆顶定时器没有到期，退出循环
        if (tmp->expire > cur)
        {
            break;
        }
        // 否则就执行堆顶定时器中的任务
        if (array[0]->cb_func)
        {
            array[0]->cb_func(array[0]->user_data);
        }
        // 将堆顶元素删除，同时生成新的堆顶定时器
        pop_timer();
        tmp = array[0];
    }
}

bool time_heap::empty() const
{
    return cur_size == 0;
}

// 下滤，确保以第hole个节点为根的子树拥有最小堆性质
void time_heap::percolate_down(int hole)
{
    heap_timer *temp = array[hole];
    int child = 0;
    for (; ((hole * 2 + 1) <= (cur_size - 1)); hole = child)
    {
        child = hole * 2 + 1; // 左子节点
        if ((child < (cur_size - 1)) && (array[child + 1]->expire < array[child]->expire))
        {
            ++child; // 如果右子节点存在（即 child < (cur_size - 1)）并且右子节点值小于左子节点值，则 child 指向右子节点。
        }
        if (array[child]->expire < temp->expire) // 如果子节点更小，交换子节点和空穴
        {
            array[hole] = array[child];
        }
        else
        {
            break;
        }
    }
    array[hole] = temp;
}

// 堆数组容量扩大一倍
void time_heap::resize() throw(std::exception)
{
    heap_timer **temp = new heap_timer *[2 * capacity];
    for (int i = 0; i < 2 * capacity; ++i)
    {
        temp[i] = NULL;
    }
    if (!temp)
    {
        throw std::exception();
    }
    capacity = 2 * capacity;
    for (int i = 0; i < cur_size; ++i)
    {
        temp[i] = array[i];
    }
    delete[] array;
    array = temp;
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
    alarm(m_TIMESLOT);  // 重新定时以不断触发 SIGALRM 信号
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