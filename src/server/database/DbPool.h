//
// Created by Administrator on 2021/3/19.
//

#ifndef NEW_POST_DbPool_H
#define NEW_POST_DbPool_H


#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include "../utility/locker.h"
#include "ChatDataBase.h"
#include <error.h>
#include <cstring>
#include <iostream>
#include <string>
using namespace std;
class DbPool
{
public:
    string m_ip;
    int m_port;
    string m_user;
    string m_password;
    string m_databaseName;
public:
    ChatDataBase* getConnection();
    bool releaseConnection(ChatDataBase *conn);
    int GetFreeConn();
    void DestroyPool();

    static DbPool *getInstance();

    void init(string ip,string user,string password,string dbname,int port,int maxConn);
    ~DbPool();
private:
    DbPool();
    
private:
    int m_maxConn; //max connection
    int m_curConn; //current connection
    int m_freeConn; //空闲 连接
    locker list_mutex;                      //互斥锁
    list<ChatDataBase *> connList;          //list容器，存放创建好的数据库对象 使用时要加锁
    sem list_sem;                           //记录数据库对象的数量 作为 资源数（信号量里的资源数）
};


//获取ChatDataBase对象
class connectionRAII{
public:
    connectionRAII(ChatDataBase *&con,DbPool *connPoll);
    ~connectionRAII();

private:
    ChatDataBase *conRAII;
    DbPool *pollRAII;
};

#endif //NEW_POST_DbPool_H
