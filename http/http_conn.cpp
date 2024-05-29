#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool)
{
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool); // 利用connectionRAII封装的RAII机制获取数据库连接

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) // 执行查询语句
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql)); // 输出错误信息
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql); // 存储查询结果

    // 返回结果集中的列数
    // int num_fields = mysql_num_fields(result); // 获取结果集中列的数量  //done num_fields用到了吗?暂时没用到

    // 返回所有字段结构的数组
    // MYSQL_FIELD *fields = mysql_fetch_fields(result); // 获取结果集中所有字段的信息  //done fields用到了吗?暂时没用到

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) // 迭代每一行数据
    {
        string temp1(row[0]); // 提取用户名
        string temp2(row[1]); // 提取密码
        users[temp1] = temp2; // 将用户名和密码存入map中
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd) // todo 有两个这个函数的定义?
{
    // 获取文件描述符的旧选项
    int old_option = fcntl(fd, F_GETFL);
    // 将非阻塞选项与旧选项进行按位或运算
    int new_option = old_option | O_NONBLOCK;
    // 将新选项设置为文件描述符的选项
    fcntl(fd, F_SETFL, new_option);
    return old_option; // 返回旧选项
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // TODO 为什么要设置为EPOLLRDHUP?
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT; // TODO 为什么要设置为EPOLLONESHOT?
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核事件表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) // todo 为什么需要重置?
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);//todo 什么地方不开oneshot什么地方开oneshot?
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;

    // m_read_idx 指向缓冲区 m_read_buf 数据末尾下一字节
    // m_checked_idx 指向从状态机当前分析的字节
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // temp 要分析的字节
        temp = m_read_buf[m_checked_idx];
        // 如果当前是 \r，则有可能读取到完整行
        if (temp == '\r')
        {
            // 下一字符达到了 buffer 结尾，则接收不完整，继续接收
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            // 下一字符是 \n，将 \r\n 改为 \0\0
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 都不符合，返回 语法错误
            return LINE_BAD;
        }
        // 如果当前字符是 \n，也可能读取到完整的行
        // 一般是上次读取到 \r，就到 buffer 末尾，没有接收完整
        // 再次接收时，就会出现这种情况
        else if (temp == '\n')
        {
            // 前一字符是 \r 则接收完整
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 没有找到 \r\n 需要继续接收
    return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
// LT或ET方式读取数据，把数据从套接字读到buf中
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    // LT读取数据
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0); // done 数据没有完整读入buffer，怎么能够在工作线程中正确解析?解析的时候，发现数据不完整，会继续接受数据
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    // ET读数据
    else
    {
        while (true)
        {
            // 从套接字接收数据，存储在m_read_buf缓冲区
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0); // todo 为什么一次性读不完 buffer2048有没有说法 http报文会不会超过2048?
            if (bytes_read == -1)
            {
                // 非阻塞ET模式下，需要一次性将数据读完
                if (errno == EAGAIN || errno == EWOULDBLOCK) // 这不是一个真正的错误，而是数据读完了
                    break;
                return false; // 发生错误
            }
            else if (bytes_read == 0) // 已经正常关闭了连接（即进行了“优雅的”关闭，发送了 FIN 包）
            {
                return false;
            }
            // 修改m_read_idx的读取字节数
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 请求行格式：
    // GET /562f25980001b1b106000338.jpg HTTP/1.1

    // 使用strpbrk函数在text中查找第一个空格或制表符（\t）的位置
    m_url = strpbrk(text, " \t");
    // 如果没有 空格 或 \t，则报文格式有误
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    // 该位置改为 \0，用于取出前面数据
    *m_url++ = '\0';
    // 取出数据，并通过与 GET 和 POST 比较，以确定请求方式
    char *method = text;

    if (strcasecmp(method, "GET") == 0) // 忽略大小写比较字符串
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1; // todo cgi处理是干什么的? 处理post请求
    }
    else
        return BAD_REQUEST;

    // m_url 此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // 将 m_url 向后偏移，通过查找，继续跳过空格和\t字符，
    // 指向请求资源的第一个字符
    m_url += strspn(m_url, " \t");
    // 查找HTTP版本号的位置
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    // 将找到的字符设置为字符串的终止符，并递增m_version指针
    *m_version++ = '\0';
    // 继续跳过空格和\t字符
    m_version += strspn(m_version, " \t");
    // 仅支持 HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    // 对请求资源前 7 个字符进行判断
    // 这里，有些报文的请求资源会代有 http://
    // 要单独处理这种情况
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/'); // 查找第一个斜杠（/）的位置
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // 如果m_url为空或不是以斜杠开头，则返回BAD_REQUEST
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求行 处理完毕，将主状态机转移去处理 请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // GET 头部信息 例子
    //  HOST: -- 服务器域名
    //  User-Agent: -- HTTP客户端程序的信息
    //  Accept: -- 用户代理，可处理的媒体类型
    //  Accept-Encoding: -- 用户代理，支持的内容编码
    //  Accept-Language: -- 自然语言集
    //  Content -Type: -- 实现主体的媒体类型
    //  Content-Length: -- 实现主体的大小
    //  Connection: -- 连接管理（Keep-Alive 或 close）
    //  空行

    // 判断 空行 还是 请求行
    if (text[0] == '\0') // 空行
    {
        if (m_content_length != 0) // POST 请求
        {
            // POST 需跳转到 消息体 处理状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST; // GET 请求
    }
    // 解析请求头部 连接字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        // 跳过 空格 和 \t 字符
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            // 如果是长连接，将 linger 标志设置为 true
            m_linger = true;
        }
    }
    // 解析请求头部 内容长度字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    // 解析请求头部 HOST字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 判断 buffer 中是否读取了消息体
    // 满足这个条件表示请求体被完整读入了（根据m_content_length来判断）
    if (m_read_idx >= (m_content_length + m_checked_idx)) // todo 这个判断条件怎么理解?
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始化从状态机状态，HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // GET 请求报文中，每一行都是 /r/n 结尾，所以对报文进行拆解时，仅用从状态机的状态( line_status = parse_line() ) == LINE_OK
    // 但，在 POST 请求报文中，消息体的末尾没有任何字符，所以不能使用 从状态机 的状态。这里转而使用 主状态机 的状态，作为循环条件入口
    //  解析完消息体后，报文的完整解析就完成了。但此时 主状态机 的状态，还是 CHECK_STATE_CONTENT、也就是说，符合循环入口条件，还会再次进入循环，
    // 这不是我们所希望的.为此，增加了&& line_status == LINE_OK，并在完成 消息体 解析后，将 line_status 变量更改为 LINE_OPEN
    // 此时可以跳出循环，完成报文解析任务
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    { // TODO 解析请求体之前，从哪获取一行的数据，因为短路求值，没有进入parse_line()吧?
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        // 主状态机 3 种状态转移逻辑
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            // 解析请求行
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            // 解析请求头
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            // 解析消息体(请求体)
            ret = parse_content(text);
            // 完整解析POST请求后，跳转报文响应函数
            if (ret == GET_REQUEST)
                return do_request(); // TODO 完成报文解析后，直接return了，不会进入后面的line_status = LINE_OPEN?
            // 解析完消息体即完成报文解析，避免再次进入循环
            // 更新 line_status
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');

    // 处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        // 以&为分隔符，前面的为用户名
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        // 以&为分隔符，后面的是密码
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);            // 分配内存空间
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES("); // 拼接SQL语句
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 判断 map 中能否找到重复的用户名
            if (users.find(name) == users.end()) // 如果在map中找不到重复的用户名
            {
                // 向数据库插入数据时，需要通过锁来同步数据
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);           // 执行SQL语句
                users.insert(pair<string, string>(name, password)); // 将用户名和密码插入map中
                m_lock.unlock();

                // 校验成功，跳转登录页面
                if (!res)
                    strcpy(m_url, "/log.html"); // 修改URL，跳转至登录页面
                // 校验失败，跳转注册失败页面
                else
                    strcpy(m_url, "/registerError.html"); // 修改URL，跳转至注册失败页面
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password) // 如果在map中找到用户名，并且密码正确
                strcpy(m_url, "/welcome.html");                             // 修改URL，跳转至欢迎页面
            else
                strcpy(m_url, "/logError.html");
        }
    }
    // 请求资源为 /0，表示跳转注册页面
    if (*(p + 1) == '0')
    {
        // 分配内存以存储 URL 字符串，使用类型转换将返回的指针转换为 char 类型指针
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        // 将 网站目录 和 /register.html 拼接
        // 更新到 m_real_file
        // m_real_file + len，表示从 m_real_file 第 len 个字符开始拷贝
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 请求资源为 /1，表示跳转登录页面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 图片页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 视频页面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        // 既不是登录，也不是注册，直接将 url 与 网站根目录 拼接
        // 这里是 welcome 界面，请求服务器的一个图片
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 通过 stat 获取 请求资源文件信息，成功 则将信息更新到
    // m_file_stat 结构体
    // 失败 返回 NO__RESOURCE 状态，表示 资源不存在
    if (stat(m_real_file, &m_file_stat) < 0) // 获取文件属性，存储在 m_file_stat 中
        return NO_RESOURCE;

    // 判断文件权限，是否可读，不可读 则返回 FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 判断文件类型，目录 则返回 BAD_REQUEST，请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过 mmap 映射文件到内存
    int fd = open(m_real_file, O_RDONLY);
    // 将一个文件 或 其他对象，映射到内存，提高文件访问速度
    // addr -- 映射区的开始地址，设置为 0 时，表示，由系统决定映射区起始地址
    // length -- 映射区长度
    // prot -- 期望的内存保护标志，不能与文件的打开模式冲突
    //      PROT_RAED 表示 页内容可以被读取
    // flags -- 指定映射对象的类型，映射选项和映射页是否可以共享
    //      MAP_PRIVATE 建立一个写入时拷贝的私有映射，内存区域的写入不会影响到原文件
    // fd -- 有效的文件描述符，一般是由 open() 函数返回
    // off_toffset -- 被映射对象内容的起点
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    // 请求文件存在，且可以访问
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool http_conn::write()
{
    int temp = 0;

    // 若要发送的数据长度为 0
    // 表示响应报文为空，一般不会出现该情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        // 在一次函数调用中，写多个非连续缓冲区，有时也将该函数称为聚集写
        // 用于一次性写入多个非连续内存区域到文件或套接字
        // m_sockfd 表示文件描述符
        // m_iv 为io 向量机制结构体 iovec
        // m_iv_count 结构体个数
        // 成功则返回 已写字节数，出错返回 -1
        // writev 以顺序 iov[0]，iov[1] 到 iov[iovcnt - 1] 从缓冲区中聚集输出数据
        // writev 返回输出的字节总数，通常，等于所有缓冲区长度之和

        // 循环调用 writev() 时，需要重新处理 iovec 中的指针 和 长度
        // 该函数不会对这两个成员做任何处理
        // writev() 的返回值为 已写字节数，但这个返回值的实用性不高
        // 因为参数传入的是 iovec 数组，计量单位是 iovcnt，而不是字节数
        // 还需要通过遍历 iovec 来计算新的基址
        // 另外，写入数据的 “结束点” 可能位于一个 iovec 中间的某个位置
        // 因此需要调整临界的 iovec 的 io_base 和 io_len
        temp = writev(m_sockfd, m_iv, m_iv_count); // DONE 不太懂? 一次性写多个非连续缓冲区

        if (temp < 0)
        {
            // 判断缓冲区是否满了
            if (errno == EAGAIN)
            {
                // 重新注册写事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        // 第一个iovec头部信息的数据已发送完，发送第二个iovec
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        // 判断条件，数据已全部发送完
        if (bytes_to_send <= 0)
        {
            unmap();
            // 在 epoll 树上重置 EPOLLONESHOT 事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            // 浏览器的请求为 长连接
            if (m_linger)
            {
                // 重新初始化 HTTP 对象
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
bool http_conn::add_response(const char *format, ...)
{
    // 写入内容超出 m_write_buf 大小就报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    // 定义可变参数列表
    va_list arg_list;
    // 变量 arg_list 初始化为传入参数
    va_start(arg_list, format);
    // 数据 format 从可变参数列表 写入 缓冲区写，返回写入数据长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 写入数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    // 清空可变参数列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    // 内部错误  500
    case INTERNAL_ERROR:
    {
        // 状态行
        add_status_line(500, error_500_title);
        // 消息报头
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    // 报文语法有误，404
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    // 资源没有访问权限，403
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    // 文件存在，200
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        // 如果请求的资源存在 大小不为0
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            // 发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            // 如果请求的资源大小为 0，返回空白 html 文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))//todo 响应正文?
                return false;
        }
    }
    default:
        return false;
    }
    // 除 FILE_REQUEST 状态外，其余状态只有申请一个 iovec
    // 指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void http_conn::process()
{
    // 调用 process_read() 处理请求
    // 并返回 HTTP_CODE 枚举类型状态码
    HTTP_CODE read_ret = process_read();
    // 请求不完整，需要继续接收
    if (read_ret == NO_REQUEST)
    {
        // 注册并监听 读事件，等待下一次数据到来
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); // done 会阻塞在这里吗? 不会
        return;
    }
    // 调用 process_write() 完成响应
    bool write_ret = process_write(read_ret);
    // 响应失败 -- 关闭连接
    if (!write_ret)
    {
        close_conn();
    }
    // 响应成功 -- 注册并监听 写事件，等待下一次写入响应数据
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
