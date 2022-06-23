//
// Created by Administrator on 2021/3/19.
//

#include "../database/DbPool.h"

//构造，初始化俩变量
DbPool::DbPool() {
    m_curConn = 0;
    m_freeConn = 0;
}

void DbPool::DestroyPool() {
    list_mutex.lock();
    while(!connList.empty())
    {
        ChatDataBase *con = connList.front();
        connList.pop_front();
        delete con;
    }
    m_curConn = 0;
    m_freeConn = 0;
    connList.clear();
    list_mutex.unlock();
    cout<<"Destroy Database Connection pool."<<endl;
}

DbPool::~DbPool() {
    DestroyPool();
}

//创建数据库连接池，记录数据库连接数，ChatDataBase对象连接数据库，根据参数maxConn，来决定有多少个对象，List容器存放多少个对象
//根据maxConn参数决定创建多少个数据库对象，都连接数据库，将创建好的数据库对象存入List容器中，且信号量记录资源数（创建了几个数据库对象）
void DbPool::init(string ip, string user, string password, string dbname, int port, int maxConn) {
    m_ip = ip;
    m_user = user;
    m_password = password;
    m_databaseName = dbname;
    m_port = port;
    for(int i =0;i<maxConn;i++)
    {
        //创建ChatDataBase对象（提供数据库服务）
        ChatDataBase *con = new ChatDataBase;
        //con对象连接数据库，可以连接其它服务器的数据库（据参数而定）
        con->initDb(ip,user,password,dbname);
        //connList是List容器（双向链表），用了类模板，此处将创建的ChatDataBase对象，放到List中
        connList.push_back(con);    //链表结尾尾添加 con对象
        ++m_freeConn;           //每添加一个对象连接数据库，计数+1
    }
    m_maxConn = m_freeConn;         //记录数据库链接的数量
    list_sem = sem(m_freeConn);     //资源数 （信号量操作）
    //打印“创建数据库连接池，数据库连接数 =”
    cout << "Create DataBase Connection pool, DataBase Connection num = " << m_maxConn<<endl;
}

//获取ChatDataBase对象  有锁操作
ChatDataBase *DbPool::getConnection() {
    //如果容器不为空
    if(connList.empty())            
        return nullptr;
    list_sem.wait();// 有连接来时候加1       //获取信号灯（信号量知识）

    //多线程访问时
    list_mutex.lock();                      //获取互斥锁
    ChatDataBase *con = connList.front();   //此函数可用于获取容器列表的第一个元素。
    connList.pop_front();                   //该函数删除列表容器的第一个元素，意味着该容器的第二个元素成为第一个元素
    m_freeConn--;
    m_curConn++;
    list_mutex.unlock();                    //释放锁

    return con;
}

//将数据库对象存到 connList容器中，操作此容器要加锁
bool DbPool::releaseConnection(ChatDataBase *conn) {
    if(conn==NULL)
    {
        return false;
    }
    list_mutex.lock();                      //获取锁
    connList.push_back(conn);               //添加元素到容器尾部，尾添加
    ++m_freeConn;
    --m_curConn;
    list_mutex.unlock();                    //释放锁
    list_sem.post();                        //发信号
    return true;
}

int DbPool::GetFreeConn() {
    return this->m_freeConn;
}

//创建静态DbPool对象
DbPool *DbPool::getInstance() {
    static DbPool connPool;
    return &connPool;
}

//构造
connectionRAII::connectionRAII(ChatDataBase *&con, DbPool *connPoll) {
    con = connPoll->getConnection();        //获取ChatDataBase对象    有锁操作
    conRAII = con;
    pollRAII = connPoll;
}

//析构函数，调用releaseConnection 将客户端conn对象添加到connList容器
connectionRAII::~connectionRAII() {
    pollRAII->releaseConnection(conRAII);
}
