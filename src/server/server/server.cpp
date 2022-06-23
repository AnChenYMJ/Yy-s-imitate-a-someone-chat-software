#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../utility/locker.h"
#include "threadpool.h"
#include "conn.h"
#include "../database/ChatDataBase.h"
#include "../database/DbPool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd, int fd, bool one_shot);   /*将文件描述符fd注册到epollfd内核事件表中，并使用边沿触发模式*/
extern int removefd(int epollfd, int fd);               /*从epollfd标识的epoll内核事件表中删除fd上的所有注册事件*/ //初始化

//预先为每个可能的客户连接分配一个conn对象
conn* conns = new conn[MAX_FD];             //连接数组      构造函数没有内容，非此处调用conn内的函数
DbPool *db_pool = DbPool::getInstance();    //构造就初始化俩变量，非此处调用相关函数//还有创建静态DbPool对象

//忽略SIGPIPE信号
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;            //在结构sigaction的实例中，指定了对特定信号的处理，信号所传递的信息，信号处理函数执行过程中应屏蔽掉哪些函数等。
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);    //断言：assert函数可以对某一行或者某个函数进行测试，用测试结果来进行判断代码或者函数的运行情况
                                                //信号安装函数：功能:sigaction函数用于改变进程接收到特定信号后的行为。
    //参考：https://blog.csdn.net/z5z5z5z56/article/details/107589787
}


void show_error(int connfd, const char *info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main (int argc, char *argv[]) {

    if (argc != 2) {
        printf("usage: %s port_number\n", argv[0]);
        return 1;
    }

    // const char *ip = argv[1];
    int port = atoi(argv[1]); //端口号
    
    //忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);//SIG_IGN：说明：信号被忽略 其它的选项：SIG_DFL：默认信号处理
    //当服务器close一个连接时，若client端接着发数据。根据TCP协议的规定，会收到一个RST响应，
    //client再往这个服务器发送数据时，系统会发出一个SIGPIPE信号给进程，告诉进程这个连接已经断开了，不要再写了。
    //不忽略会出问题，参考：https://www.cnblogs.com/lit10050528/p/5116566.html

    //创建线程池 //用了模板头：类模板，template<typename T> 
    //就是创建了线程，将线程都放到一个数组中（类型是pthread_t[]） 
    threadpool<conn>* pool = NULL;              //threadpool的构造函数就是与客户端交互的功能入口,创建线程时指定了线程函数（功能入口）
    try {
        pool = new threadpool<conn>;
    }
    catch (...) {
        fprintf(stderr, "catch some exception when create threadpool.\n");
        return 1;
    }

    //void DbPool::init(string ip, string user, string password, string dbname, int port, int maxConn)
    //创建数据库连接池，记录数据库连接数，ChatDataBase对象连接数据库，根据参数maxConn，来决定有多少个对象，List容器存放多少个对象
    //根据maxConn参数决定创建多少个数据库对象，都连接数据库，将创建好的数据库对象存入connList容器中，且信号量记录资源数（创建了几个数据库对象）
    db_pool->init("121.36.69.144", "root", "123456", "tengxun", 3306, 8);   //创建数据库连接池  此处的IP的变量其实根本没用到，可以忽略
    
    printf("测试1\n");

    assert(conns);          //assert函数可以对某一行或者某个函数进行测试，用测试结果来进行判断代码或者函数的运行情况。
    int user_count = 0;

    //创建socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);     
    assert(listenfd >= 0);
    struct linger tmp = {1, 0};//设置为强制退出          //此结构体设置优雅断开或强制断开，参考：https://blog.csdn.net/teethfairy/article/details/10917145
    //setsockopt：Socket描述符选项
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));//参考：https://www.cnblogs.com/cthon/p/9270778.html
    //第二个参数level是被设置的选项的级别，如果想要在套接字级别上设置选项，就必须把level设置为 SOL_SOCKET。
    //SO_LINGER，如果选择此选项, close或 shutdown将等到所有套接字里排队的消息成功发送或到达延迟时间后>才会返回. 否则, 调用将立即返回。

    //保存IP、端口号、协议簇到sockaddr_in结构体中
    int ret = 0;
    struct sockaddr_in address;         //socket赋值相关、必须的结构体 （标准如此，非自定义）
    bzero(&address, sizeof(address));   //bzero函数：将内存（字符串）前n个字节清零
    address.sin_family = AF_INET;
    //inet_pton：这个函数的功能：将IP地址从字符串格式转换成网络地址格式，支持Ipv4和Ipv6.
    inet_pton(AF_INET, "0.0.0.0", &address.sin_addr);   //为何是0.0.0.0        IPV4中，0.0.0.0地址被用于表示一个无效的，未知的或者不可用的目标。
                                                        //在服务器中，0.0.0.0指的是本机上的所有IPV4地址，如果一个主机有两个IP地址，192.168.1.1 和 10.1.2.1，
                                                        //并且该主机上的一个服务监听的地址是0.0.0.0, 那么通过两个ip地址都能够访问该服务。
                                                        //在路由中，0.0.0.0表示的是默认路由，即当路由表中没有找到完全匹配的路由的时候所对应的路由。
    address.sin_port = htons(port);                     //存入端口号

    //socket绑定socket与IP、端口号、协议簇
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    //开启监听
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 结构体epoll_event被用于注册所感兴趣的事件和回传所发生待处理的事件，
    //就是存放事件的数组
    epoll_event events[MAX_EVENT_NUMBER];
    //epoll_create，打开一个epoll文件描述符。也理解为：创建epoll句柄，size（5）就是你在这个epoll fd上能关注的最大socket fd数
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    /*将服务器socket（可看作一种文件描述符）注册到epollfd内核事件表中，并使用边沿触发模式，将socket设置为非阻塞I/O（看函数定义的注释）*/
    addfd(epollfd, listenfd, false);    //add listenfd to epollfd
    conn::m_epollfd = epollfd;

    //循环等待处理 事件触发
    while (true) 
    {
        //等待 epoll 文件描述符 epfd上的事件 最长 超时毫秒。事件指向的内存区域  将包含调用者可用的事件。epoll_wait ()最多  返回 maxevents 。
        //指定-1 则是无期限的等待
        //等待epollfd监视的socket的事件响应，响应后会返回到events数组中（事件列表）
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        //epoll 返回，遍历返回成功的事件列表，对每个事件分别判断
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd; //触发事件的fd                    
            if (sockfd == listenfd) {//有新用户建立连接
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);               //socklen_t 与 int有差不多的长度
                int connfd = accept(listenfd, (struct sockaddr *)&client_address,   //返回客户端socket，客户端相关信息自动存入client_address中
                                            &client_addrlength);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (connfd >= MAX_FD) {
                    fprintf(stderr, "connfd >= MAX_FD\n");
                    continue;
                }
                if (conn::m_user_count >= MAX_FD) {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                //初始化新接受的连接，m_user_count+1
                conns[connfd].init(connfd, client_address);
                printf("Debug info: accept one connect, connection count = %d\n", conn::m_user_count);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //如果有异常，直接关闭客户端连接
                conns[sockfd].close_conn();
                printf("Debug info: user count = %d\n", conn::m_user_count);                                   
            }               //EPOLLIN：可读
            else if (events[i].events & EPOLLIN) {
                //根据读的结果，决定将任务添加到线程池，还是关闭连接
                if (conns[sockfd].read()) {//读取客户端传送的数据
                    printf("read_buf:\n %s", conns[sockfd].m_read_buf);
                    pool->append(conns + sockfd);   //将客户端的请求加入请求队列，该操作会唤醒线程池中的一个线程为该客户端进行处理
                }
                else {
                    printf("Debug info: user count = %d\n", conn::m_user_count);
                    conns[sockfd].close_conn();             
                }
            }               //EPOLLOUT：可写
            else if (events[i].events & EPOLLOUT) {
                //根据写的结果，决定是否关闭连接，调用process_write函数
                if (!conns[sockfd].process_write()) {
                    conns[sockfd].close_conn();
                    printf("Debug info: user count = %d\n", conn::m_user_count);
                }
            }
            else {
                fprintf(stderr, "epoll return from an unexpected event\n");
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete[] conns;
    delete pool;
    delete db_pool;
    return 0;
}