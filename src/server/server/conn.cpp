#include <cstring>
#include "json/json.h"
#include <iostream>
#include <string>
#include "../database/ChatDataBase.h"
#include "conn.h"
#include <unistd.h>
#include "../utility/common.h"
#include <vector>
#include <unordered_map>
#include "../database/DbPool.h"

using namespace std;

locker lock;
//map容器
unordered_map<int, int> mp; //userid --> sockfd 
extern conn* conns;         //连接数组
extern DbPool *db_pool;     //数据库连接池

//epoll文件描述符相关操作，开始~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*将文件描述符fd设置为非阻塞*/
int setnonblocking(int fd) {
    //fcntl：根据文件描述词来操作文件的特性。
	int old_option = fcntl(fd, F_GETFL);         //F_GETFL：取得文件描述符fd的文件状态标志,如同下面的描述一样(arg被忽略)
	int new_option = old_option | O_NONBLOCK;    //O_NONBLOCK：非阻塞I/O;如果read()调用没有可读取的数据,或者如果write()操作将阻塞,read或write调用返回-1和EAGAIN错误
    //设置属性，将文件描述符fd设置为非阻塞I/O
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

/*将文件描述符fd注册到epollfd内核事件表中，并使用边沿触发模式*/  //看笔记
void addfd(int epollfd, int fd, bool one_shot) {
	struct epoll_event event;
	event.data.fd = fd;                            //文件描述符fd（文件句柄，文件打开时产生）
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; //边沿触发模式，读取数据应该一次性读取完毕，就是让传进来的fd设置为边沿触发
	if (one_shot) //如果one_shot传入true
    {
        //EPOLLONESHOT事件：当一个线程在处理某个socket时，其他线程是不可能有机会操作该socket的
		event.events |= EPOLLONESHOT;   //只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里。
	}
    //epoll_ctl，用于操作epoll函数所生成的实例，参考：https://blog.csdn.net/zhoumuyu_yu/article/details/112477150
    //在epollfd内核事件表中，注册文件描述符fd
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);  //EPOLL_CTL_ADD：在文件描述符epfd所引用的epoll实例上注册目标文件描述符fd，并将事件事件与内部文件链接到fd。
	setnonblocking(fd); //所有加入epoll监视的文件描述符都是非阻塞的
}

/*从epollfd标识的epoll内核事件表中删除fd上的所有注册事件*/
void removefd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);   //EPOLL_CTL_DEL：从epfd引用的epoll实例中删除（注销）目标文件描述符fd。
	close(fd);                                  //关闭释放fd
}

//6.    //10.   //11.3  //18.
//对epollfd监视的文件描述符fd，添加ev事件
void modfd(int epollfd, int fd, int ev) {
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
//epoll文件描述符相关操作，结束~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


//init
int conn::m_user_count = 0;
int conn::m_epollfd = -1;           //记录epollfd epoll文件描述符 （epollfd用于监视关注socket）

//服务器和客户端建立连接时调用该函数，sockfd是该链接的服务器本地sock，addr是客户端的地址   
/*初始化新接受的连接*/
void conn::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;          //将客户端socket赋值给m_sockfd
	m_address = addr;
	m_userId = -1;

    //以下两行避免TIME_WAIT状态，仅用于调试，实际使用应去掉
	int reuse = 1;
	setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //将文件描述符sockfd注册到m_epollfd内核事件表中，并使用边沿触发模式，开启EPOLLONESHOT事件
	addfd(m_epollfd, sockfd, true); 
	m_user_count++;

    //调用该函数，初始化所有数据
	init();
}

//init all data member
//初始化链接的所有成员
void conn::init() {//长连接，调用该函数，初始化所有数据
    //初始化链接的所有成员
    memset(m_read_buf, '\0', sizeof m_read_buf);
    m_read_idx = 0;
    memset(m_write_buf, '\0', sizeof m_write_buf);
    m_write_idx = 0;
    cmd = "";
    reply = "";
    m_checked_idx = 0;
}


//关闭和客户端的连接
void conn::close_conn(bool real_close /*= true*/) {
    if (real_close && m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);  //从epoll的监视集合中删除连接
		m_sockfd = -1;
		m_user_count--;	//关闭一个连接，客户总数减一
	}
    //将用户状态修改为下线
    if (m_userId != -1) {
        ChatDataBase *db_conn;
        connectionRAII connRAII(db_conn, db_pool);//使用RAII技术从数据库连接池中获取一条数据库连接
        printf("Debug info: userId = %d offline\n", m_userId);
        lock.lock();
        db_conn->my_database_offline(m_userId);
        m_userId = -1;
        mp.erase(m_userId);
        lock.unlock();
    }
}


//循环读取客户数据，直到无数据可读或者对方关闭连接，注意epoll为边沿触发模式，一次读取数据要读到阻塞
//主线程调用，读取数据
bool conn::read() {
	if (m_read_idx >= READBUFSIZE) {
		return false;
	}

    //这里是否需要考虑将m_read_idx重新置零，不能重新将m_read_idx置零，一次收到的json可能不完整
	int bytes_read = 0;
	while (true) {
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READBUFSIZE - 
											m_read_idx, 0);
		if (bytes_read == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {//非阻塞io，读到EWOULDBLOCK说明数据已经读取完毕
				printf("Debug info: read done! EWOULDBLOCK.\n");
				break;
			}
			return false;               //读取出现未知错误
		}
		else if (bytes_read == 0) {
			return false;
		}
		m_read_idx += bytes_read;       //读取到的字符数
	}
	return true;
}


//2.
//process_read()中调用，处理用户登录的请求   将传入的json对象里的信息获取
//获取并比对数据库里 传入用户 的账号信息，存入m_write_buf，发送给客户端
void conn::do_login(const Json::Value &json) {
    int userid = json["userId"].asInt();            //将"userId" 对应的数据转为 int类型
    string password = json["password"].asString();  //将"password"对应的数据转为 字符串类型

    Json::Value ret;                                //创建对象
    ret["cmd"] = "login-reply";                     //存入一段数据
    
    ChatDataBase *db_conn;                          //创建对象
    connectionRAII connRAII(db_conn, db_pool);      //从被锁的conList容器获取 数据库连接池里的一个数据库对象 

    //对比客户端发过来的id、密码 与数据库里的 id、密码 是否吻合（my_database_password_correct）
    if ( db_conn->my_database_password_correct(userid, password) ) {
        //返回一个登录成功的提示信息，并将用户状态修改为在线
        ret["success"] = 1;
        
        //将用户状态设置为上线
        m_userId = userid;         //记录id 
        printf("login debug, m_userId = %d, m_sockfd = %d\n", m_userId, m_sockfd);
        lock.lock();               //获取互斥锁
        mp[m_userId] = m_sockfd;   //上线则将客户端socket加入map中   用map容器标记是否在线，map中有对应id和fd，则说明在线
        lock.unlock();             //释放锁
    } else {
        //登陆失败，id、密码与数据库里的不吻合
        ret["success"] = 0;
    }
    User u; //登录返回用户信息  User结构体（自定义），存放用户信息
    db_conn->my_database_user_information(u, userid);   //查询数据库内，userid的用户信息，将用户信息存入User结构体

    //将User结构体内的，用户信息，转为json格式版
    ret["user"] = userToJsonObj(u); //用户信息的json格式版
    reply = ret.toStyledString();   //ret的数据生成字符串 存到reply 中 reply是一个string对象
    /*
    if (reply.size() >= WRITEBUFSIZE) {
        fprintf(stderr, "error: write buffer overflow\n");
        return false;
    }
    */
    //copy to send buffer，这里有缓冲区溢出的风险，后期考虑加上判断
    memcpy(m_write_buf, reply.c_str(), reply.size());       //reply数据复制到m_write_buf
    m_write_buf[reply.size()] = '\0';                       //加上字符串结尾标志'\0'
}

//3.
//获取数据库中：好友列表、好友消息 相关信息，按一定顺序规则，存入reply 和 m_write_buf（发送缓冲区）
void conn::do_get_friend_list(const Json::Value &root) {
    int userid = root["userId"].asInt();                    //将"userId" 对应的数据转为 int类型
    
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);              //此对象构造函数：获取ChatDataBase对象，锁操作  （应该是让数据库对应的连接池操作加锁）
    vector<User> users;                                     //类数组容器
    vector<int> msgNum;
    db_conn->my_database_friend_list(users, msgNum, userid); //users是返回的列表  //查询两张数据库表的信息，存入俩vector容器里，俩容器分别代表：好友列表、好友消息
    Json::Value arr;
    //将数据库中获取的信息：好友列表、好友消息，转为json格式存入arr。
    for (int i = 0; i < users.size(); i++) {                //size：返回元素个数，这里代表了好友列表的用户数
        Json::Value temp = userToJsonObj(users[i]);         //将对应元素转为json格式，存入temp
        temp["msgNum"] = msgNum[i];                         //好友消息存在temp 键"msgNum" 下
        arr.append(temp);                                   //将Json::Value添加到数组末尾，就是temp作为子Value添加到arr里
    }
    
    //将arr存入到ret，ret转为字符串格式存入reply
    Json::Value ret;
    ret["cmd"] = "getFriendList-reply";
    ret["user"] = arr; //如果arr为空，那么ret["user"] = null
    reply = ret.toStyledString();                           

    //将json数据拷贝到发送缓冲区中
    memcpy(m_write_buf, reply.c_str(), reply.size());   
    m_write_buf[reply.size()] = '\0';
}

//1.
//从 root 中解析出客户端发过来的注册信息，存入数据库，再将数据库生成的ID，和一段数据存入m_write_buf，发送给客户端
void conn::do_register(const Json::Value &root) {
    //获取客户端发来的信息
    string userName = root["userName"].asString();                  //用户名
    string password = root["password"].asString();                  //密码
    ChatDataBase *db_conn;                                          //db_pool是静态变量，在server.cpp里调用Dbpool的函数创建的
    connectionRAII connRAII(db_conn, db_pool);                      //从connList容器中获取数据库对象 //析构函数将客户端conn对象添加到connList容器，就是用完了就返回到容器里
    int userId;
    db_conn->my_database_user_password(userName, password, userId); //向表T_USER中插入用户名、密码、头像、个性签名等数据，并查询刚插入的的f_user_id的数值，赋值给userid（传参数时用了引用）
    
    Json::Value ret;                                                //创建Value对象
    ret["cmd"] = "register-reply";                                  //赋值
    ret["userId"] = userId;
    reply = ret.toStyledString();                                   //生成字符串 存到reply 中 reply是一个string对象 在conn::init被初始化，每次调用process_write函数（写内容发送）都初始化一遍

    //从reply中复制size()个字符到 m_write_buf中
    memcpy(m_write_buf, reply.c_str(), reply.size());               //c_str()函数返回一个指向正规C字符串的指针, 内容与本字符串相同. 
                                                                    //size()函数返回字符串中现在拥有的字符数。
    //m_write_buf的末尾添加元素'\0'（字符串的结束标志）
    m_write_buf[reply.size()] = '\0';                               
}

//4.
//向T_CHAT_MESSAGE表中插入收发双方数据、消息数据，查询接收方ID是否上线
void conn::do_insert_private_message(const Json::Value &root) {
    Message msg;
    msg.sendId = root["sendId"].asInt();        //发送者ID
    msg.receiveId = root["receiveId"].asInt();  //接收者ID
    msg.type = root["type"].asInt();            //类型
    msg.content = root["content"].asString();   //发送的消息
    msg.time = root["time"].asString();         //时间

    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);  //构造函数中：获取ChatDataBase对象，锁操作  （应该是让数据库对应的连接池操作加锁）
    //向T_CHAT_MESSAGE表中插入收发双方数据、消息数据，查询接收方ID是否上线
    db_conn->my_database_chat(msg); //插入消息记录
}

//7.
//获取俩ID用户之间的聊天内容等信息（ID等），转为特定json格式顺序，存入发送缓冲区
void conn::do_get_message_record(const Json::Value &root) {
    int userId1 = root["userId1"].asInt(); //sender 在线的人，发送该请求的用户
    int userId2 = root["userId2"].asInt(); //receiver

    ChatDataBase *db_conn;                  
    connectionRAII connRAII(db_conn, db_pool);
    vector<Message> msg;                    //创建vector容器
    //获取俩ID用户之间的聊天内容，存入msg容器中，同时T_USER_RELATIONSHIP表内容更新（俩ID间未读消息归零）
    db_conn->my_database_chat_search(msg, userId1, userId2);
    Json::Value arr;
    for (int i = 0; i < msg.size(); i++) {
        arr.append(messageToJsonObj(msg[i]));//将容器msg的元素依次添加到arr数组末尾
    }
    //创建新json对象，赋值，将获取到的俩ID用户之间的聊天内容等信息存入
    Json::Value ret;
    ret["cmd"] = "getMessageRecord-reply";
    ret["sendId"] = userId2;                
    ret["message"] = arr;

    //ret存入reply
    reply = ret.toStyledString();
    //reply 存入 发送缓冲区
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//8.
//让对应ID设定为离线状态（修改T_USER表的f_online标识）
void conn::do_offline(const Json::Value &root) {
    int userId = root["userId"].asInt();
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    //更新T_USER表，对应ID设为 离线状态：f_online设为0.
    db_conn->my_database_offline(userId);

    //写下"NONE"，存入 发送缓冲区
    Json::Value ret;
    ret["cmd"] = "NONE";
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//9.
//查询userId是否存在，存在的话，获取俩ID是否为好友关系，将关系写入发送缓冲区
void conn::do_search_user_by_id(const Json::Value &root) {
    int userId = root["userId"].asInt();
    int sendId = root["sendId"].asInt();
    User u;
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    Json::Value ret;
    ret["cmd"] = "searchUserById-reply";
    //查询T_USER表中userId的信息，存在相关信息则返回true
    if (!db_conn->my_database_user_id_exist(userId)) {
        //如果不存在
        ret["exist"] = false;                         //"exist"写入false，存进发送缓冲区
        reply = ret.toStyledString();
        memcpy(m_write_buf, reply.c_str(), reply.size());
        m_write_buf[reply.size()] = '\0';
        return;
    }
    //如果存在
    ret["exist"] = true;
    //查询数据库内，userId的用户信息，将用户信息存入User结构体
    db_conn->my_database_user_information(u, userId); //查找用户信息
    ret["user"] = userToJsonObj(u);                   //用户信息存入ret 的"user"
    
    //查询俩ID是否为好友关系（T_USER_RELATIONSHIP中俩ID是否绑定），是好友的话返回true
    bool already = db_conn->my_database_is_friend(userId, sendId);//判断是否是好友
    ret["already"] = already;//存入好友关系

    //好友关系（是或否）写进发送缓冲区
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}
    
//5.    //10.   //11.2  //18.
//查询传入id 是否上线 ，上线的话将该id在mp容器里对应的sock，存入sock。返回true
int conn::isOnline(int userId, int &sock) {
    sock = -1;              //sock赋值为-1，用到引用，所以能直接修改（非本函数局部变量）
    lock.lock();            //获取互斥锁
    //用户在线  { 在do_login（登录功能中） 用map容器标记是否在线，map中有对应id和fd，则说明在线 }
    if (mp.count(userId)) { //count()函数返回map中键值等于key的元素的个数。
        sock = mp[userId];  //sock记录userId对应的数据
        printf("userId = %d is online, sock = %d\n", userId, sock);
        lock.unlock();      //释放锁
        return true;
    }
    lock.unlock();          //释放锁
    return false; //用户不在线
}

//10.
//接收者在线的话，向他发送 发送者的用户信息（存入发送缓冲区），不在线、且不为好友关系，表T_FRIEND_NOTIFICATION插入一条记录
void conn::do_add_friend(const Json::Value &root) {
    int sendId = root["sendId"].asInt();
    int receiveId = root["receiveId"].asInt();
    
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    //如果接受者在线
    int sock = -1;
    //查询receiveId是否在线（是否登录）
    if (isOnline(receiveId, sock)) {//isOnline(userId, sock)，如果用户在线返回true，sock为该用户与服务器的文件描述符，否则sock返回-1
        //在线的话，向接受者发送通知
        Json::Value notify;
        notify["cmd"] = "addFriendNotification";
        User u;
        //查询数据库内，sendId的用户信息，将用户信息存入User结构体
        db_conn->my_database_user_information(u, sendId);//给接受者返回发送者的信息
        notify["user"] = userToJsonObj(u);

        conns[sock].reply = notify.toStyledString();
        //写入 发送缓冲区
        memcpy(conns[sock].m_write_buf, conns[sock].reply.c_str(), conns[sock].reply.size());
        conns[sock].m_write_buf[conns[sock].reply.size()] = '\0';
        printf("add friend send to receiveer:\n %s", conns[sock].m_write_buf);
        //对m_epollfd监视的文件描述符conns[sock].m_sockfd，添加EPOLLOUT事件
        modfd(m_epollfd, conns[sock].m_sockfd, EPOLLOUT);
    } else {//不在线，向数据库中插入一条记录
        //判断俩ID是否为好友，不是的话，向T_FRIEND_NOTIFICATION插入俩ID绑定在一起
        db_conn->my_database_add_new_friend_notification(sendId, receiveId);
    }

    //向发送者回应消息
    Json::Value ret;
    ret["cmd"] = "addFriend-reply";
    ret["success"] = 1;

    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//11.1
//俩ID插入T_USER_RELATIONSHIP表，向在线的 俩ID发送添加好友的Json
void conn::do_add_friend_confirm(const Json::Value &root) {
    int sendId = root["sendId"].asInt();
    int receiveId = root["receiveId"].asInt();

    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    //俩ID插入T_USER_RELATIONSHIP表
    db_conn->my_data_base_add_friend(sendId, receiveId); //接受者点击了确定添加好友，将好友之间的关系添加到数据库中，这个函数需要修改

    //如果发送者在线，向发送者发送添加好友的Json
    int sock = -1;
    if (isOnline(sendId, sock)) {                       //查询传入id 是否上线 ，上线的话将该id在mp容器里对应的sock，存入sock。返回true
        Json::Value temp;
        temp["cmd"] = "addUserToFriendList";
        User t;
        //查询数据库内，receiveId的用户信息，将用户信息存入User结构体
        db_conn->my_database_user_information(t, receiveId);
        temp["user"] = userToJsonObj(t);

        
        conns[sock].reply = temp.toStyledString();
        memcpy(conns[sock].m_write_buf, conns[sock].reply.c_str(), conns[sock].reply.size());
        conns[sock].m_write_buf[conns[sock].reply.size()] = '\0';
        printf("Debug info: sender = %d is online, sock = %d send addFriendToList\n json:%s\n", sendId, sock, reply.c_str());
        //对m_epollfd监视的文件描述符sock，添加EPOLLOUT事件
        modfd(m_epollfd, sock, EPOLLOUT);
    } //如果发送者不在线，上线时，直接获取好友列表即可

    //接受者一定在线，向接受者发送添加好友的Json
    Json::Value ret;
    ret["cmd"] = "addUserToFriendList";
    User u;
    //查询数据库内，sendId的用户信息，将用户信息存入User结构体
    db_conn->my_database_user_information(u, sendId);//向接受者发送发送者的用户信息
    ret["user"] = userToJsonObj(u);

    //存入 发送缓冲区 
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//12.
//获取与被申请者userId对应的申请者 的用户信息，存入发送缓冲区，发送给客户端
void conn::do_read_add_friend_notification(const Json::Value &root) {
    int userId = root["userId"].asInt();
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    vector<User> users;
    //查找在T_FRIEND_NOTIFICATION中和 userId 绑定的那个ID在T_USER里的信息，尾添加到users中
    db_conn->get_all_add_friend_notification(userId, users);  //获取所有接受者为userId的好友申请信息，返回申请者的用户信息
    
    Json::Value ret, arr;
    ret["cmd"] = "readAddFriendNotification-reply";
    for (auto &u: users) {
        arr.append(userToJsonObj(u));//将u当作元素添加到 arr 数组中
    }
    ret["user"] = arr;              //arr作为子value 添加到ret的"user"下

    //写入 发送缓冲区
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//13.
//再俩表中记录操作：创建群聊、群聊成员增加并记录（管理员），发送群聊的相关信息给客户端
void conn::do_create_group(const Json::Value &root) {//ok，还未测试
    //获取客户端发来的创建者ID、群聊名称
    int userId = root["userId"].asInt();
    string groupName = root["groupName"].asString();

    int groupId = -1;
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    //创建群聊：表T_GROUP插入俩数据：群聊名称，群聊拥有者ID（组ID用了主键自增长，不需要额外传递）  群聊成员增加：T_GROUP_MEMBER表中插入俩数据：组ID、拥有者ID   
    //再获取 组ID，被groupId记录（传递的是引用）
    db_conn->my_database_add_new_group(groupName, userId, groupId); //这个函数还没有实现，已添加

    //群聊信息写入ret
    Json::Value ret;
    ret["cmd"] = "createGroup-reply";
    Group g(userId, groupId, groupName);
    ret["group"] = groupToJsonObj(g);

    //群聊信息 写入 发送缓冲区 ，发送给客户端
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//14.
//查找组是否存在，组中是否存在sendId的成员，并将该组的相关信息、查找成功与否结果 发送给客户端
void conn::do_search_group_by_id(const Json::Value &root) {
    //提取 组ID 发送者ID
    int groupId = root["groupId"].asInt();
    int sendId = root["sendId"].asInt();
    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    Json::Value ret;
    ret["cmd"] = "searchGroupById-reply";
    //查找T_GROUP中是否存在 组ID为传入groupId 的相关信息，查到了返回true
    if (!db_conn->my_database_group_exist(groupId)) {
        //没查到则向客户端发送 键"exist"值为false 的信息
        ret["exist"] = false;
        reply = ret.toStyledString();
        memcpy(m_write_buf, reply.c_str(), reply.size());
        m_write_buf[reply.size()] = '\0';
        return ;
    }
    //查到了则向客户端发送 键"exist"值为true 的信息
    ret["exist"] = true;

    //通过群Id查找群：T_GROUP中查找 组ID为groupId 的组相关信息，并存入传入的Group结构体
    Group group;
    db_conn->my_database_search_group(group, groupId);
    ret["group"] = groupToJsonObj(group);
    
    //T_GROUP_MEMBER中查找 组id为groupId 并且 成员id为sendId 的那信息存不存在，成功查到了返回true
    bool already = db_conn->my_database_in_group(sendId, groupId);
    //将是否存在的 结果 存入ret
    ret["already"] = already;

    //将ret发送给客户端
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//15.
//用户加入群聊、通过群Id查找群并返回群的相关信息，将信息发送给客户端
void conn::do_request_add_group(const Json::Value &root) {
    //提取信息
    int userId = root["userId"].asInt();
    int groupId = root["groupId"].asInt();
    int ownerId = root["ownerId"].asInt();

    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    //用户加入群聊    T_GROUP_MEMBER中插入俩数据：组ID、成员ID
    db_conn->my_database_user_add_group(groupId, userId); //将用户插入群关系表中
    
    Json::Value ret;
    ret["cmd"] = "addGroupToList";                      //json信息，要执行的功能标识"cmd"
    Group group;
    //通过群Id查找群：T_GROUP中查找 组ID为groupId 的组相关信息，并存入Group结构体
    db_conn->my_database_search_group(group, groupId);  //获取新加入群的信息返回给用户
    //查到的组的相关信息 存入ret
    ret["group"] = groupToJsonObj(group);               

    //ret发送给客户端
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//16.
//上线时发送下面的json，获取所有群列表  获取每个群的 群相关信息、历史消息数，发送给客户端
void conn::do_get_group_list(const Json::Value &root) {
    //提取信息
    int userId = root["userId"].asInt();

    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    vector<Group> groups;           //群相关信息
    vector<int> msgNum;             //历史消息数
    //通过一个用户id获取该用户加入的所有群聊，并获取相应的历史消息数、群ID、群名、群拥有者ID
    db_conn->my_database_get_group(groups, msgNum, userId);

    Json::Value ret, arr;
    ret["cmd"] = "getGroupList-reply";
    //获取每个群的 群相关信息、历史消息数 存入temp，temp再作为元素添加到arr数组
    for (int i = 0; i < groups.size(); i++) {
        auto temp = groupToJsonObj(groups[i]);
        temp["msgNum"] = msgNum[i];
        arr.append(temp);
    }
    ret["group"] = arr;     //arr存进 ret的"group"
    
    //ret发送给客户端
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//18.
//所有在该群中的 离线成员ID 在表T_GROUP_MEMBER 中的未读群聊消息数（+1）
//通过群id查询群中所有成员的信息，并判断在线的成员，将在线的成员信息写入 conns的 发送缓冲区，并注册可写事件
//给客户端发送 一条信息 "success" == 1
void conn::do_send_group_message(const Json::Value &root) {
    //提取数据
    int sendId = root["sendId"].asInt();
    int groupId = root["groupId"].asInt();
    string content = root["content"].asString();
    string time = root["time"].asString();

    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);

    Message msg;
    msg.sendId = sendId;
    msg.receiveId = groupId;
    msg.content = content;
    msg.time = time;
    //所有在该群中的 离线成员ID 在表T_GROUP_MEMBER 中的未读群聊消息数（+1）
    db_conn->my_database_group_msg_insert(msg);//在数据库的消息记录表中插入一条数据
    vector<User> user;
    //查询群groupid中 所有成员的 成员ID，将这些成员ID的用户信息，存入传进来的vector容器
    db_conn->my_database_get_group_user(groupId, user);//通过群id查询群中所有成员的信息，后期可以修改为获得在线的群成员
    //将该群中 所有成员 循环判断一遍是否在线
    for (auto u: user) {
        int sock = -1;
        //如果userId 是在线（已登录），将在线的成员信息写入 conns的 发送缓冲区，并注册可写事件
        if (isOnline(u.userId, sock)) {
            Json::Value ret;
            ret["cmd"] = "receiveGroupMessage";
            ret["sendId"] = sendId;
            ret["groupId"] = groupId;
            ret["content"] = content;
            ret["time"] = time;

            conns[sock].reply = ret.toStyledString();
            memcpy(conns[sock].m_write_buf, conns[sock].reply.c_str(), conns[sock].reply.size());
            conns[sock].m_write_buf[conns[sock].reply.size()] = '\0';
            //对m_epollfd监视的文件描述符sock，添加EPOLLOUT事件
            modfd(m_epollfd, sock, EPOLLOUT);//注册可写事件
        }
    }
    //给客户端发送 一条信息 "success" == 1
    Json::Value ret;
    ret["cmd"] = "sendGroupMessage-reply";
    ret["success"] = 1;
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//17.
//获取 组ID为groupId 的群内所有成员的信息，该群中userId的所有聊天信息（获取玩清零），发送给客户端
void conn::do_open_group(const Json::Value &root) {
    //提取信息
    int groupId = root["groupId"].asInt();
    int userId = root["userId"].asInt();

    ChatDataBase *db_conn;
    connectionRAII connRAII(db_conn, db_pool);
    
    Json::Value ret, uarr, marr;
    ret["cmd"] = "openGroup-reply";
    ret["groupId"] = groupId;
    ret["userId"] = userId;
    

    vector<User> users;
    //查询群groupid中 所有成员的 成员ID，将这些成员ID的用户信息，存入users容器
    db_conn->my_database_get_group_user(groupId, users);
    //循环将这些成员ID的用户信息 存入temp，temp再作为元素添加到uarr数组
    for (auto user: users) {
        Json::Value temp;
        temp["userId"] = user.userId;
        temp["userName"] = user.userName;
        temp["iconStr"] = user.iconStr;
        uarr.append(temp);
    }
    //uarr存入ret的"user"
    ret["user"] = uarr;     

    //获取 群ID为groupId 的群里所有聊天消息的信息，存进msgs容器，再清除userId在该群中的未读消息数
    vector<Message> msgs;
    db_conn->my_database_get_group_chat_msg(groupId, userId, msgs);
    //聊天内容等相关信息存入temp，temp再作为元素添加到marr数组
    for (auto msg: msgs) {
        Json::Value temp;
        temp["sendId"] = msg.sendId;
        temp["content"] = msg.content;
        temp["time"] = msg.time;
        marr.append(temp);
    }
    //marr存入ret的"msg"
    ret["msg"] = marr;

    //ret发送给客户端
    reply = ret.toStyledString();
    memcpy(m_write_buf, reply.c_str(), reply.size());
    m_write_buf[reply.size()] = '\0';
}

//处理读取到的用户请求，用枚举作为返回值类型
PROCESS_CODE conn::process_read() {
    //首先判断是否读取到了完整的json
    int left_bracket_num = 0, right_bracket_num = 0, idx = 0, complete = 0;
    // while (idx < m_read_idx && m_read_buf[idx] != '{') idx++;//json 解析开始
    // if (idx >= m_read_idx) {
    //     return DONE;
    // }
                                                                            //m_read_idx 在read函数中，赋值为读到的字符数
    while (idx < m_read_idx && m_read_buf[idx]) {
        if (m_read_buf[idx] == '{') left_bracket_num++;                     //如果读到'{'   left_bracket_num自加，
        else if (m_read_buf[idx] == '}') right_bracket_num++;               //如果读到'}'   right_bracket_num自加，一个 { 加 一个 } 就是一组完整的json数据 
        idx++;                                                              //下标自加
        //如果读取到了完整的json
        if (left_bracket_num == right_bracket_num && right_bracket_num != 0) {//读取到了完整的json
            complete = 1;                                                   //complete设为1，起到标识json读取完整的作用
            //m_read_buf[idx] = 0;
            break;
        }
    }
    // if (idx >= m_read_idx) {
    //     return DONE;
    // }
    //如果没有读到完整数据，打印提示。 读到整数据的话，complete是会设为1的
    if (!complete) {
        fprintf(stdout, "json data format is incomplete \n");
        return INCOMPLETE;
    }

    //读取到了完整的json，下面调用jsoncpp，读取json的内容
    Json::Reader reader;        //创建对象，读取JSON数据相关
	Json::Value root;           //用于接收解析后的json数据

    int cnt = 0;
    //解析Json字符串，如果没有解析成功，则执行if内语句      //将json文件流或字符串解析到Json::Value, 主要函数有Parse。
    if (!(cnt = reader.parse(m_read_buf, root))) {      // reader将Json字符串解析到root，root将包含Json里所有子元素  //root 用于接收解析后的json数据（在上面有声明该对象）
        fprintf(stderr, "error, parse json error");     //将错误信息送到标准错误文件中。
        return INCOMPLETE;  //ERROR                     //返回INCOMPLETE（枚举中设定为代表了 json数据不完整）
    }

    // printf("m_read_buf[idx] = %c, m_read_buf + idx = %s", m_read_buf[idx], m_read_buf + idx);
    // while (idx < READBUFSIZE && (m_read_buf[idx] == '\0' || m_read_buf[idx] == '\t' || m_read_buf[idx] == '\n') ) idx++;
    
    
    //root 用于接收解析后的json数据           //这是别人写的第3方库，没找到详细介绍（注释）原作在github：https://github.com/open-source-parsers/jsoncpp
    //将解析后的Json数据转换为字符串           //有一个中文对此库的介绍，但是不知道全不全：https://www.saoniuhuo.com/article/detail-233360.html
    string cmd = root["cmd"].asString();    //asString：将此JSON值作为字符串返回，假设此值表示JSON字符串。如果不是这样，则引发异常。
                                            //看来键 "cmd" 是整个json数据的键，最顶层的键

    if (cmd == "register") {                                //注册
        printf("Debug info: deal with register\n");                             //调试信息：处理寄存器
        do_register(root);                                  //1.                //从root中解析出客户端发过来的注册信息，存入数据库，再将数据库生成的ID，和一段数据存入m_write_buf 和 reply
        printf("Debug info: register reply json: %s", reply.c_str());           //调试信息： 注册回复 json：
        //return REPLY;
    } else if (cmd == "login") {                            //登录
        printf("Debug info: deal with login");
        do_login(root);                                     //2.                //获取并比对数据库里 传入的json对象 的账号信息，存入reply和m_write_buf（发送缓冲区）
        printf("Debug info: login reply json:\n%s,", reply.c_str());
        //return REPLY;
        
        //返回用户的好友列表，包含未读取的离线消息
    } else if (cmd == "getFriendList") {                    //获取好友列表
        printf("Debug info: deal with getFriendList\n");
        do_get_friend_list(root);                           //3.                //获取数据库中：好友列表、好友消息 相关信息，按一定顺序规则，存入reply 和 m_write_buf（发送缓冲区）
        printf("Debug info: get friend list reply:\n%s,", reply.c_str());
        //return REPLY;
    } else if (cmd == "sendMessage") {                      //发送消息
        printf("Debug info: deal with sendMessage\n");
        do_insert_private_message(root);                    //4.                //向T_CHAT_MESSAGE表中插入收发双方数据，查询接收方ID的在线信息



        //如果用户在线，发送消息(json)
        //"receiveId"对应数据转为int类型
        int receiveId = root["receiveId"].asInt();
        int sock = -1;
        if (isOnline(receiveId, sock)) {//在线，获得接受者的socket   //5.        //查询传入id 是否上线 ，上线的话将该id在mp容器里对应的sock，存入sock。返回true
            //将root数据（解析后的json数据）存入conns对象
            root["cmd"] = "receiveMessage";
            conns[sock].reply = root.toStyledString();
            //conns对象的reply内数据存入 发送缓冲区，末尾加 ‘\0’ （字符串结尾标识）
            memcpy(conns[sock].m_write_buf, conns[sock].reply.c_str(), conns[sock].reply.size());
            conns[sock].m_write_buf[conns[sock].reply.size()] = '\0';
            printf("reply json:\n", conns[sock].reply.c_str());
            modfd(m_epollfd, sock, EPOLLOUT);//注册可写事件          //6.         //对m_epollfd监视的文件描述符sock，添加EPOLLOUT事件
        }
        

        Json::Value temp;
        temp["cmd"] = "NONE";
        reply = temp.toStyledString();//temp转为字符串格式存入reply
        //reply存进 发送缓冲区 m_write_buf
        memcpy(m_write_buf, reply.c_str(), reply.size());
        m_write_buf[reply.size()] = '\0';
        printf("Debug info: sendMessage reply json:\n %s\n", reply.c_str());
        //return REPLY;
    } else if (cmd == "getMessageRecord") {                 //查找聊天记录  
        printf("Debug info: deal with getMessageRecord\n");
        do_get_message_record(root);                        //7.                //获取俩ID用户之间的聊天内容等信息（ID等），转为特定json格式顺序，存入发送缓冲区
        printf("Debug info: getMessageRecord reply json:\n%s\n", reply.c_str());
        //return REPLY;
    } else if (cmd == "offline") {                          //离线
        printf("Debug info: deal with offline\n");
        do_offline(root);                                   //8.                //让对应ID设定为离线状态（修改T_USER表的f_online标识）
        printf("Debug info: offline reply:\n %s\n", reply.c_str());
        //return REPLY;
    } else if (cmd == "searchUserById") { //搜索好友时，根据用户id查找获得一个用户的所有信息
        printf("Debug info: deal with searchUserById\n");
        do_search_user_by_id(root);                         //9.                //查询userId是否存在，存在的话，获取俩ID是否为好友关系，将关系写入发送缓冲区
         printf("Debug info: searchUserById reply\n %s\n", reply.c_str());
        //return REPLY;
    } else if (cmd == "addFriend") {                        //添加用户请求
        printf("Debug info: deal with addFriend\n");
        do_add_friend(root);                                //10.               //接收者在线的话，向他发送 发送者的用户信息（存入发送缓冲区）
        printf("Debug info: addFriend reply\n %s\n", reply.c_str());            //不在线、且不为好友关系的话，表T_FRIEND_NOTIFICATION插入一条记录
        //return REPLY;
    } else if (cmd == "addFriendConfirm") {                 //添加好友确认
        printf("Debug info: deal with addFriendConfirm\n");
        do_add_friend_confirm(root);                        //11.               //俩ID插入T_USER_RELATIONSHIP表，向在线的 俩ID发送添加好友的Json
        printf("Debug info: add friend confirm reply\n %s\n", reply.c_str());
        //return REPLY;   //将发送者的信息返回给接受者
    } else if (cmd == "readAddFriendNotification") { //上线时发送该请求，读取所有离线的添加好友请求
        printf("Debug info: deal with readAddFriendNotification\n");
        do_read_add_friend_notification(root);              //12.               //获取与被申请者userId对应的申请者 的用户信息，存入发送缓冲区，发送给客户端
        printf("Debug info: readAddFriendNotification reply\n %s\n", reply.c_str());
        //return REPLY;
    } else if (cmd == "createGroup") {                      //创建组（群聊）
        printf("Debug info: deal with createGroup\n");
        do_create_group(root);                              //13.               //再俩表中记录操作：创建群聊、群聊成员增加并记录（管理员），发送群聊的相关信息给客户端
        printf("Debug info: createGroup reply\n %s\n", reply.c_str());
    } else if (cmd == "searchGroupById") {                  //用ID搜索群聊
        printf("Debug info: deal with seachGroupById\n");
        do_search_group_by_id(root);                        //14.               //查找组是否存在，组中是否存在sendId的成员，并将该组的相关信息、查找成功与否结果 发送给客户端
        printf("Debug info: seachGroupById reply\n %s\n", reply.c_str());
    } else if (cmd == "requestAddGroup") {                  //申请加入群
        printf("Debug info: deal with requestAddGroup\n");
        do_request_add_group(root);                         //15.               //用户加入群聊、通过群Id查找群并返回群的相关信息，将信息发送给客户端
        printf("Debug info: requestAddGroup reply\n %s\n", reply.c_str());
    } else if (cmd == "getGroupList") {                     //获取群列表
        printf("Debug info: deal with getGroupList\n");
        do_get_group_list(root);                            //16.               //获取所有群列表  获取每个群的 群相关信息、历史消息数，发送给客户端
        printf("Debug info: getGroupList reply\n %s\n", reply.c_str());
    } else if (cmd == "openGroup") {                        //打开群（聊天界面）
        printf("Debug info: deal with openGroup\n");
        do_open_group(root);                                //17.               //获取 组ID为groupId 的群内所有成员的信息，该群中userId的所有聊天信息（获取玩清零），发送给客户端
        printf("Debug info: openGroup reply\n %s\n", reply.c_str());
    } else if (cmd == "sendGroupMessage") {                 //发送群内的聊天信息
        printf("Debug info: deal with sendGroupMessage\n");                     
        do_send_group_message(root);                        //18.               //所有在该群中的 离线成员ID 在表T_GROUP_MEMBER 中的未读群聊消息数（+1）
                                                                                //通过群id查询群中所有成员的信息，并判断在线的成员，将在线的成员信息写入 conns的 发送缓冲区，并注册可写事件
                                                                                //给客户端发送 一条信息 "success" == 1
        printf("Debug info: sendGroupMessage reply\n %s\n", reply.c_str());
    }

    // while (idx < m_read_idx && m_read_buf[idx] != '{') idx++;//json 解析开始
    // if (idx < m_read_idx && m_read_buf[idx] == '{') { //可能m_read_buf里收到了多个json请求
    //     memcpy(m_read_buf, m_read_buf + idx, READBUFSIZE - idx);
    //     modfd(m_epollfd, m_sockfd, EPOLLIN);//添加可读事件，尝试再次读取json
    //     printf("Again %s\n", m_read_buf + idx);
    //     printf("m_read_buf = %s\n", m_read_buf);
    //     printf("idx = %c\n", m_read_buf[idx]);
    //     process_read();
    // }
    return REPLY;
}

//由线程池中的工作线程调用，这是处理客户请求的入口函数
//threadpool中创建了线程，线程函数中创建了conn对象并调用了此函数
void conn::process() {
    //PROCESS_CODE枚举，默认从0开始向后，0、1、2.....
    //此处枚举来接住返回值
	PROCESS_CODE read_ret = process_read();  //process_read函数：处理读取到的用户请求， 定义就在上面     
    //如果JSON数据不完整
	if (read_ret == INCOMPLETE) {            //INCOMPLETE == 0，用于标识JSON数据不完整
		modfd(m_epollfd, m_sockfd, EPOLLIN); //继续读取数据   //对epollfd监视的文件描述符m_sockfd， 添加EPOLLIN事件：连接到达；有数据来临；读
		return;
	}
    //如果有数据返回客户端
	else if (read_ret == REPLY) {//已经准备好要写回的json，存放在reply       //REPLY：标识有数据返回客户端
        modfd(m_epollfd, m_sockfd, EPOLLOUT);	//对m_sockfd添加对可写事件，此时主线程epoll_wait返回，处理写事件//EPOLLOUT事件：有数据要写；写
    } 
    // else {
    //     modfd(m_epollfd, m_sockfd, EPOLLIN);
    // }
}


//sock可写时，主线程epoll返回，会调用该函数，向客户端写入数据
bool conn::process_write() {
    int temp = 0;
	int bytes_have_send = m_write_idx;//数据如果没有一次性发完，接着发送数据， m_write_idx在init 中初始化成0 用于记录已发送的字节数
	int bytes_to_send = reply.size() - m_write_idx;//发送一个字符串结束符 '\0'       bytes_to_send记录还要发送的字符数 
    //如果没有可以写的内容，则设置为可读状态
	if (bytes_to_send == 0) {
		modfd(m_epollfd, m_sockfd, EPOLLIN);    //可读
		init();                                 //初始化
		return true;
	}
    
	while (1) {
        //向TCP写缓冲区复制要发送内容，返回写入的字符数
		temp = write(m_sockfd, m_write_buf + bytes_have_send, bytes_to_send);   //m_write_idx在init 中初始化成0 用于记录已发送的字节数 bytes_to_send表示可以发送的的内容字符数
        //写入TCP写缓冲区出错了
		if (temp <= -1) {
			if (errno == EAGAIN) {
				/*如果TCP写缓冲区没有空间，等待下一轮EPOLLOUT事件。虽然在此期间，
				服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性*/
				modfd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			fprintf(stderr, "write error\n");
			return false;
		}
		bytes_to_send -= temp;          //bytes_to_send记录还要发送的字符数 
		m_write_idx += temp;            //m_write_idx记录已经发送的字符数
		bytes_have_send = m_write_idx;
		if (bytes_have_send >= reply.size()) { //数据写完了
			//init(); //长连接，重新初始化连接的内容

            //--------------由于使用了epolloneshot事件，一定要加上下面这句话，让其他线程有机会处理该连接-------------
			modfd(m_epollfd, m_sockfd, EPOLLIN);    //重置socket上的EPOLLONESHOT事件，让其他线程有机会继续处理这个socket
            break;
		}
	}
    //初始化连接资源
    init(); //数据传输完毕，清空连接缓存
	return true;
}