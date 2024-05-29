#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 添加定时器，内部调用私有成员 add_timer
// 公有 add_timer() 是外部调用接口，只有一个参数，
// 即要插入的 定时器对象
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head)
    {
        head = tail = timer;
        return;
    }
    // 如果新的定时器 超时时间，小于当前头节点
    // 将当前定时器节点作为 头节点
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 否则调用私有成员，调整内部节点
    add_timer(timer, head);
}

// 调整定时器，任务发生变化时，调整定时器在链表的位置
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    // 被调整的定时器在 链表尾部
    // 定时器超时值，仍小于下一个定时器超时值，不调整
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // 被调整定时器，是链表头节点，将定时器取出，重新插入
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 被调整定时器在内部，将定时器取出，重新插入
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

// 删除定时器
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 链表中只有一个定时器，直接删除
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 被删除定时器为头节点
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 为尾节点
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 被删除定时器在链表内部，常规删除
    // 符合前面情况的都 return 了，这里就不用 if
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
// 定时任务处理函数：删除所有超时的定时器
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    // 获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;
    // 遍历定时器链表
    while (tmp)
    {
        // 链表容器为升序排列
        // 当前时间小于定时器超时时间，后面定时器也未到期
        // expire是固定的超时时间，越后面的越晚超时
        if (cur < tmp->expire)
        {
            break;
        }
        // 当前定时器到期，则调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);
        // 将处理后的定时器，从链表容器删除，并重置头节点
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

// 私有成员，被公有成员 add_timer 和 adjust_timer 调用
// 用于调整链表内部节点，
// 即 私有 add_timer() 是类内部调用的函数，接受 2 个参数
// 将 timer 插入到 lst_head 之后合适位置
// timer -- 要插入的定时器
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;  // 插入的起点
    util_timer *tmp = prev->next; // 起点下一位置
    // 遍历当前节点之后的链表，按照超时时间，升序插入
    while (tmp)
    {
        // 由于公有 add_timer()
        // 此时timer的超时时间，一定 > lst_head
        if (timer->expire < tmp->expire)
        {
            // 插入 prev 和 prev->next 之间
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        // prev 和 prev_next 一直往后移动
        prev = tmp;
        tmp = tmp->next;
    }
    // 如果此时 prev 为尾节点，tmp 为 空
    // timer 超时时间 > 尾节点 超时时间
    if (!tmp)
    {
        // timer需要作为新的尾节点
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
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
    if (restart)//TODO 自动重启的作用?
        sa.sa_flags |= SA_RESTART;
    // 所有信号添加到 信号集 中,会阻塞所有其他信号
    sigfillset(&sa.sa_mask);
    // 执行 sigaction() 函数
    assert(sigaction(sig, &sa, NULL) != -1);//对信号设置新的处理方式
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();// 处理链表上到期的定时器
    alarm(m_TIMESLOT);// 重新定时以不断触发 SIGALRM 信号
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
