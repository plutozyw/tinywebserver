#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;	// 当前连接数
	m_FreeConn = 0; // 空闲连接数
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	// 初始化数据库信息
	m_url = url;			 // 数据库地址
	m_Port = Port;			 // 数据库端口
	m_User = User;			 // 用户名
	m_PassWord = PassWord;	 // 密码
	m_DatabaseName = DBName; // 数据库名称
	m_close_log = close_log; // 日志开关

	// 创建 MaxConn 条数据库连接
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;	   // MySQL 连接指针
		con = mysql_init(con); // 初始化连接

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error"); // 错误信息
			exit(1);				  // 退出程序
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0); // 连接数据库

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error"); // 错误信息
			exit(1);				  // 退出程序
		}
		// 更新连接池和空闲连接数量
		connList.push_back(con);
		++m_FreeConn;
	}

	// 信号量初始化为最大连接数
	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn; // 最大连接数等于空闲连接数
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL; // MySQL 连接指针

	if (0 == connList.size()) // 如果连接池为空，返回空指针
		return NULL;

	// 取出连接，信号量原子 -1，为 0 则等待
	reserve.wait();

	lock.lock(); // 加锁

	con = connList.front(); // 获取连接池中的第一个连接
	connList.pop_front();	// 弹出连接

	--m_FreeConn; // 空闲连接数减一
	++m_CurConn;  // 当前连接数加一

	lock.unlock();
	return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con) // 如果连接为空，返回false
		return false;

	lock.lock();

	connList.push_back(con); // 将连接放回连接池
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	// 释放连接原子 +1
	reserve.post();
	return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)// 如果连接池不为空
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);// 关闭数据库连接
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();// 清空连接池列表
	}

	lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool(); // 销毁连接池
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}