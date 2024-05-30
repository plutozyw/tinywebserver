#include "time_heap.h"
#include "../http/http_conn.h"

heap_timer::heap_timer(int delay)
{
    expire = time(NULL) + delay;
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

void time_heap::adjust_timer(heap_timer *timer)
{
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