//
// Created by Administrator on 2021/3/16.
//
#include <stdlib.h>
#include <vector>
#include "../database/ChatDataBase.h"
ChatDataBase::ChatDataBase()
{
    /*my_database_connect("tengxun");*/
    mysql = mysql_init(NULL);
    if(mysql == NULL)
    {
        cout << "Error:" << mysql_error(mysql);
        exit(1);
    }
}

ChatDataBase::~ChatDataBase()
{
    /*mysql_close(mysql);*/
    if(mysql != NULL)  //关闭数据连接
    {
        mysql_close(mysql);
    }
}


//暂定：未使用
void ChatDataBase::my_database_get_group_member(string name, string &s)
{
    char sql[1024] = {0};
    sprintf(sql, "select member from %s;", name.c_str());
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
    }

    MYSQL_RES *res = mysql_store_result(mysql);
    if (NULL == res)
    {
        cout << "mysql_store_result error" << endl;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (NULL == row[0])
    {
        return;
    }
    s += row[0];
}

//1. 
//向表T_USER中插入用户名、密码、头像、个性签名等数据，并查询末尾的f_user_id的数值，赋值给userid（传参数时用了引用）（conn.cpp里的do_register函数的userid变量）
void ChatDataBase::my_database_user_password(string name, string password,int &userid)  
{
    int imgIdx = rand() % 9;            //0~8随机数
    char sql[128] = {0};
    char iconStr[100] = {0};
    //iconStr 存入图片（头像）
    sprintf(iconStr, ":/media/person/media/person/%d.jpg", imgIdx); 
    //sql存入sql语句：向表中插入行：用户名、密码、用户ID、头像、个性签名等数据 
    sprintf(sql, "insert into T_USER (f_user_name,f_password,f_online,f_photo,f_signature) values('%s','%s','%d','%s','%s');", name.c_str(),password.c_str(),0,iconStr,"happy");
    //函数执行一条 MySQL 查询，就是sql里存的的语句。执行由“Null终结的字符串”查询指向的SQL查询
    if (mysql_query(mysql,sql))
    {
        //执行错误就会打印错误码
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //执行语句sql清零
    memset(sql, 0, sizeof(sql));
    //sql存入语句   f_user_id：获取自建ID（建表时创建，自增的数值：自增ID）
    sprintf(sql, "SELECT max(f_user_id) from T_USER ;");    
    //执行 sql 里的语句，
    if (mysql_query(mysql,sql)) 
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //MYSQL_RES是 MYSQL C API 中的用于做为资源的结构体，
    //mysql_store_result()将查询的全部结果读取到客户端，分配1个MYSQL_RES结构，并将结果置于该结构中。
    //将执行语句后的，查询结果集（字符串地址）返回到客户端，存进res中（就是本服务器，数据库可能在别的服务器）
    MYSQL_RES *res = mysql_store_result(mysql);
    //检索结果集res的下一行。
    MYSQL_ROW row = mysql_fetch_row(res);
    //将row[0]从string 转换为int类型
    userid = stoi(row[0]);
}

//2.1
//客户端登录时调用，对比客户端发过来的id、密码 与数据库里的 id、密码 是否吻合
bool ChatDataBase::my_database_password_correct(int id, string password)
{
    char sql[256] = {0};
    //查找f_user_id为id的 f_password信息，将语句存进sql
    sprintf(sql, "select f_password from T_USER where f_user_id='%d';",id); 
    if (mysql_query(mysql, sql) != 0)   //执行sql 里的语句
    {
        cout << "mysql_query error" << endl;
    }
    //将执行语句后的，查询结果集（字符串地址）返回本服务器，存进res中
    MYSQL_RES *res = mysql_store_result(mysql);
    //检索结果集res的下一行。
    MYSQL_ROW row = mysql_fetch_row(res);
    //如果传进来的password 与 数据库里的f_password 相同
    if (row && password == row[0])
    {
        //sql内清零
        memset(sql,0,sizeof(sql));
        //sql存入一条语句，更新数据库，让数据库f_user_id 为id 的 f_online=1
        sprintf(sql,"UPDATE T_USER SET f_online=1 where f_user_id='%d';",id);
        if (mysql_query(mysql, sql) != 0) { //执行sql 里的语句
            //执行失败
            cout << "update user online information error" << endl;
            return false;
        }
        else
        {
            //成功执行
            //cout<<"update user online information success" <<endl;
            return true;
        }
    }
    //传进来的password 与 数据库里的f_password 不一样
    else
    {
        cout<<"user no exist or password false"<<endl;
        return false;
    }
}


//9.3
//查询俩传入ID是否为好友关系（T_USER_RELATIONSHIP中俩ID是否绑定），是好友的话返回true
bool ChatDataBase::my_database_is_friend(int n1, int n2)
{
    char sql[128] = {0};
    //查询俩ID 在表T_USER_RELATIONSHIP中的信息，俩ID被绑定则说明加了好友
    sprintf(sql, "SELECT * FROM T_USER_RELATIONSHIP WHERE T_USER_RELATIONSHIP.f_user_id1 = '%d' AND T_USER_RELATIONSHIP.f_user_id2 = '%d';", n1, n2);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
    }
    //返回结果集地址
    MYSQL_RES *res = mysql_store_result(mysql);
    //返回结果集中字段数
    int  num_rows=mysql_num_rows(res); 
    //如果num_rows不为0，说明俩ID在T_USER_RELATIONSHIP中绑定了，俩ID是好友关系
    if (num_rows) {  //已经是好友了
        return true;
    }
    //俩ID不是好友关系
    else {
        return false;
    }
}

//10.2
//判断俩ID是否为好友，不是的话，向T_FRIEND_NOTIFICATION插入俩ID绑定在一起
void ChatDataBase::my_database_add_new_friend_notification(int n1, int n2)
{
    //查询俩ID是否为好友关系（T_USER_RELATIONSHIP中俩ID是否绑定），是好友的话返回true
    if(my_database_is_friend(n1, n2))
    {
        cout<<"friend already"<<endl;
        return ;
    }
    char sql[1024] = {0};
    //n1 发送 好友请求  n2 接受
    //T_FRIEND_NOTIFICATION插入俩数据
    sprintf(sql,"insert into T_FRIEND_NOTIFICATION (f_user_id1,f_user_id2) values('%d','%d')",n1,n2);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "insert notification error" << endl;
    } 
    // else{
    //     cout << "insert notification  success" << endl;
    // }
}

//14.1
//查找T_GROUP中是否存在 组ID为传入ID 的相关信息，查到了返回true
bool ChatDataBase::my_database_group_exist(int gid)
{
    char sql[128] = {0};
    //表T_GROUP中查询 组ID为gid 的相关信息
    sprintf(sql, "select * from T_GROUP WHERE f_group_id = '%d';", gid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
    }
    //返回结果集地址，返回结果集中字段数（就是有几个查询结果）
    MYSQL_RES *res = mysql_store_result(mysql);
    int  num_rows = mysql_num_rows(res); 
    //没有查询结果
    if (num_rows == 0)//群不存在
    {
        return false;
    }
    //有查询结果
    else//群存在
    {
        return true;
    }
}

//3.
//查询两张数据库表的信息，存入俩vector容器里，俩容器分别是：好友列表、好友消息
void ChatDataBase::my_database_friend_list(vector<User> &friendlist,vector<int> &friend_message, int userid) {
    char sql[1024] = {0};
    //查询T_USER_RELATIONSHIP 与 T_USER 表里的信息
    sprintf(sql, "select f_user_id2,f_user_name,f_online,f_photo,f_signature,f_message_num from T_USER_RELATIONSHIP, T_USER where T_USER_RELATIONSHIP.f_user_id1 = '%d' and T_USER_RELATIONSHIP.f_user_id2 = T_USER.f_user_id", userid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
        return;
    }
    // else
    // {
    //     cout << "mysql_query success" <<endl;
    // }
    //返回查询结果集（地址），存入res
    auto res = mysql_store_result(mysql);
    //返回结果集中字段的数。
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    //打印结果集字段数
    cout<<numField<<endl;   
    /*for(int i=0;i<numField;i++)
    {
        cout<<mysql_fetch_field(res)->name<<endl;
    }*/
    //检索结果集内容，分类获取 用户信息，存入temp（User结构体）
    while((row=mysql_fetch_row(res))!=NULL)
    {
        User temp;
        for(int i=0;i<numField;i++)
        {
            if(i==0)
                temp.userId=stoi(row[i]);
            if(i==1)
                temp.userName=row[i];
            if(i==2)
                temp.online=stoi(row[i]);
            if(i==3)
                temp.iconStr=row[i];        //头像
            if(i==4)
                temp.desc=row[i];           //签名
        }
        //vector容器尾添加元素，添加获取到的用户信息，以用户为单位
        friendlist.push_back(temp);
        //尾添加：将f_message_num 的信息 存入friend_message（好友消息）
        friend_message.push_back(stoi(row[5]));
    }
}

//9.1
//查询T_USER表中传入ID的信息，存在相关信息则返回true
bool ChatDataBase::my_database_user_id_exist(int userid) {
    char sql[128] = {0};
    //查询T_USER表中userid 的所有信息
    sprintf(sql, "select * from T_USER WHERE f_user_id = '%d';", userid);
    if (mysql_query(mysql, sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return false;
    }
    //返回结果集地址
    MYSQL_RES *res = mysql_store_result(mysql);
    //返回结果集中字段数
    int  num_rows = mysql_num_rows(res);
    if (num_rows) {
        cout << "my_database_user_id_exist return true" << endl;
        return true;
    }
    cout << "my_database_user_id_exist return false" << endl;
    return false;
}

//暂定：未使用
bool ChatDataBase::my_database_group_id_exist(int groupid) {
    char sql[128] = {0};
    sprintf(sql, "select f_group_id from T_GROUP WHERE f_group_id = '%d';", groupid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
        return false;
    }

    MYSQL_RES *res = mysql_store_result(mysql);
    int  num_rows = mysql_num_rows(result);
    if (num_rows) {//该群存在
        return true;
    }
    return false;
}

//2.2   //9.2       //10.1      //11.
//查询数据库内，userid的用户信息，将用户信息存入User结构体
void ChatDataBase::my_database_user_information(User &ur, int userid) {
    char sql[1024] = {0};
    //查询f_user_id 为 userid 的那行 在数据库里的信息
    sprintf(sql, "select f_user_id,f_user_name,f_online,f_photo,f_signature from T_USER where f_user_id='%d';", userid);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //返回查询语句结果集（一个地址），存入res
    auto res = mysql_store_result(mysql);
    //mysql_num_fields是指函数返回结果集中字段的数。
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    //检索res 的下一行，就是获取查询到的数据，存入row
    while((row=mysql_fetch_row(res))!=NULL)
    {
        //分类将row里的数据，提取出来
        for(int i=0;i<numField;i++)
        {
            if(i==0)
                ur.userId=stoi(row[i]); //stoi函数：将数字字符转化位int输出·
            if(i==1)
                ur.userName=row[i];
            if(i==2)
                ur.online=stoi(row[i]);
            if(i==3)
                ur.iconStr=row[i];
            if(i==4)
                ur.desc=row[i];
        }
    }
}

//4.1
//向T_CHAT_MESSAGE表中插入收发双方数据，查询接收方ID的在线信息
void ChatDataBase::my_database_chat(Message ms) {
    char sql[1024] = {0};
    //向T_CHAT_MESSAGE表中插入数据：发送、接收双方ID、发送的信息、发送时的时间、类型
    sprintf(sql, "INSERT into T_CHAT_MESSAGE(f_senderid,f_targetid,f_msgcontent,f_sendtime,f_type) VALUES('%d','%d','%s','%s','%d');", ms.sendId,ms.receiveId,ms.content.c_str(),ms.time.c_str(),ms.type);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_insert error" << endl;
        return;
    }
    // else
    // {
    //     cout << "mysql_insert success" <<endl;
    // }
    //查询传入ID的在线信息，成功返回true
    bool temp = my_database_user_state(ms.receiveId);
    if(temp == false)   //如果接收方ID不在线
    {
        //打印提示信息（“不在线”）
        cout<<ms.receiveId<<"not online"<<endl; 
        //sql清空
        memset(sql,0,sizeof(sql));
        //双方ID更新到T_USER_RELATIONSHIP表
        sprintf(sql,"update T_USER_RELATIONSHIP SET f_message_num=f_message_num+1 WHERE f_user_id1='%d' and f_user_id2='%d';",ms.receiveId,ms.sendId);
        if (mysql_query(mysql, sql) != 0)
        {
            cout<<"error"<<endl;
        }
    }

}

//7.
//获取俩ID用户之间的聊天内容，存入传入的vector容器中，同时T_USER_RELATIONSHIP表内容更新（俩ID间未读消息归零）
void ChatDataBase::my_database_chat_search(vector<Message> &messageList, int senderid, int targetid) {
    char sql[1024] = {0};
    //查询T_CHAT_MESSAGE表内，俩传入ID对应的一些信息：聊天内容等。
    sprintf(sql, "select f_senderid,f_targetid,f_msgcontent,f_sendtime,f_type from T_CHAT_MESSAGE where (f_senderid='%d' and f_targetid='%d')or(f_senderid='%d' and f_targetid='%d')", senderid,targetid,targetid,senderid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
        return;
    }
    // else
    // {
    //     cout << "mysql_query success" <<endl;
    // }
    //返回查询结果集地址
    auto res = mysql_store_result(mysql);       // 关键字auto是用来声明完全可选择的局部变量的
    //返回结果集中字段的数。
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    Message temp;
    //获取结果集内容，存入row，再从row获取信息存入temp对象，把temp尾添加到传入的容器中
    while((row=mysql_fetch_row(res))!=NULL)
    {
        for(int i=0;i<numField;i++) {
            //cout << i<<":"<<row[i] << endl;
            if(i==0){temp.sendId=stoi(row[i]);} //string类型转换为int
            if(i==1){temp.receiveId=stoi(row[i]);}
            if(i==2){temp.content=row[i];}
            if(i==3){temp.time=row[i];}
            if(i==4){temp.type=stoi(row[i]);}
        }
        //传入的vector容器尾添加元素 temp
        messageList.push_back(temp);
    }
    //sql清空
    memset(sql,0,sizeof(sql));
    //更新T_USER_RELATIONSHIP表，未读消息标识f_message_num 归0
    sprintf(sql,"update T_USER_RELATIONSHIP SET f_message_num=0 WHERE f_user_id1='%d' and f_user_id2='%d';", senderid,targetid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
        return;
    }
}

//暂定：未使用
bool ChatDataBase::search_user(int userid, User &ur) {
    if(my_database_user_id_exist(userid))
    {
        my_database_user_information(ur,userid);
        return true;
    }
    else{
        return false;
    }
}

//暂定：未使用
void ChatDataBase::my_database_add_new_friend(int sender_id, int receive_id) {
    char sql[1024] = {0};
    memset(sql, 0, sizeof(sql));
    sprintf(sql,"select f_user_id1,f_user_id2 from T_FRIEND_NOTIFICATION where f_user_id1='%d' and f_user_id2='%d';",sender_id,receive_id);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "insert friend relationship information error" << endl;
    }
    // else
    // {
    //     cout << "insert friend relationship information success" << endl;
    // }
    MYSQL_RES *res = mysql_store_result(mysql);
    MYSQL_ROW row = mysql_fetch_row(res);
    if(row!=NULL){
        memset(sql, 0, sizeof(sql));
        sprintf(sql,"insert T_USER_RELATIONSHIP(f_user_id1,f_user_id2)values('%d','%d');",sender_id,receive_id);
        if(mysql_query(mysql,sql)!=0){
            cout<<"insert error"<<endl;
        }
        memset(sql, 0, sizeof(sql));
        sprintf(sql,"insert T_USER_RELATIONSHIP(f_user_id1,f_user_id2)values('%d','%d');",receive_id,sender_id);
        if(mysql_query(mysql,sql)!=0){
            cout<<"insert error"<<endl;
        }
        memset(sql, 0, sizeof(sql));
        sprintf(sql,"delete from T_FRIEND_NOTIFICATION where f_user_id1='%d' and f_user_id2='%d';",sender_id,receive_id);
        if(mysql_query(mysql,sql)!=0){
            cout<<"delete error"<<endl;
        }
    }
}

//暂定：未使用
void ChatDataBase::my_database_search_notification(int userid, User &ur) {
    char sql[1024] = {0};
    memset(sql, 0, sizeof(sql));
    sprintf(sql,"select f_user_id2 FROM T_FRIEND_NOTIFICATION where f_user_id1='%d';",userid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "select error" << endl;
    }
    // else
    // {
    //     cout << "select success" << endl;
    // }
    auto res = mysql_store_result(mysql);
    MYSQL_ROW row = mysql_fetch_row(res);
    //cout<<row[0]<<endl;
    auto temp = row[0];
    int userid2 = stoi(temp);
    search_user(userid2,ur);
}

//8.
//更新T_USER表，对应ID设为 离线状态：f_online设为0.
void ChatDataBase::my_database_offline(int userid) {
    char sql[128];
    memset(sql,0,sizeof(sql));
    //更新T_USER表，对应ID设为 离线状态：f_online设为0.
    sprintf(sql,"UPDATE T_USER SET f_online=0 where f_user_id='%d';",userid);
    if (mysql_query(mysql, sql) != 0) {
        cout << "update user offline information error" << endl;
        return ;
    }
    // else
    // {
    //     cout<<"update user offline information success" <<endl;
    // }
}

//暂定：未使用
void ChatDataBase::my_database_search_group_notification(int groupownerid, vector<User> &userList) {
    char sql[128];
    memset(sql,0,sizeof(sql));
    sprintf(sql,"SELECT f_user_id from T_GROUP_NOTIFICATION where f_type='add' and f_group_owner_id='%d';",groupownerid);
    if(mysql_query(mysql,sql) != 0)
    {
        cout<<"get users of group failed"<<endl;
    }
    // else{
    //     cout<<"get users of group success"<<endl;
    // }
    MYSQL_RES *res = mysql_store_result(mysql);
    MYSQL_ROW row;
    User temp;
    while((row=mysql_fetch_row(res))!=NULL)
    {
        cout<<row[0]<<endl;
        my_database_user_information(temp,stoi(row[0]));
        userList.push_back(temp);
    }
}

//暂定：未使用
void ChatDataBase::my_database_group_owner_add_user(int groupid, int userid) {
    char sql[128];
    memset(sql,0,sizeof(sql));
    sprintf(sql,"insert into T_GROUP_MEMBER(f_group_id,f_user_id) values('%d','%d')",groupid,userid);
    if(mysql_query(mysql,sql) !=0)
    {
        cout<<"insert failed"<<endl;
    }
    // else{
    //     cout<<"insert success"<<endl;
    // }
}

//暂定：未使用
void ChatDataBase::my_database_delete_friend(int userid, int friendid) {
    char sql[128];
    memset(sql,0,sizeof(sql));
    sprintf(sql,"delete from T_USER_RELATIONSHIP where f_user_id1='%d' and f_user_id2='%d';",userid,friendid);
    if(mysql_query(mysql,sql)!=0)
    {
        cout<<"query error"<<endl;
        return ;
    }
    // else{
    //     cout<<"query success" <<endl;
    // }
    memset(sql,0,sizeof(sql));
    sprintf(sql,"delete from T_USER_RELATIONSHIP where f_user_id1='%d' and f_user_id2='%d';",friendid,userid);
    if(mysql_query(mysql,sql)!=0)
    {
        cout<<"query error"<<endl;
        return ;
    }
    // else{
    //     cout<<"query success" <<endl;
    // }
}

//4.2
//查询传入ID的在线信息（f_online就是在线信息），成功返回true
bool ChatDataBase::my_database_user_state(int userid) {
    char sql[128];
    //sql清空
    memset(sql,0,sizeof(sql));  
    //查询用户ID的在线信息（f_online就是在线信息）
    sprintf(sql,"select T_USER.f_online from T_USER where f_user_id='%d';",userid);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return false; //抛出异常还是 返回false
    }
    //返回查询结果集地址
    MYSQL_RES *res = mysql_store_result(mysql);
    //获取结果集内容，存入row
    MYSQL_ROW row = mysql_fetch_row(res);
    //将row[0]从string 转换为int类型
    if(stoi(row[0])==1)             //成功返回true
        return true;
    else{
        return false;               //失败返回false
    }
}

//创建数据库连接池使用
//连接数据库，可以连接其它服务器的数据库（据参数而定） 在Dbpool.cpp中的 init函数调用
bool ChatDataBase::initDb(string host, string user, string passwd, string db_name) 
{
    //host 决定连接类型，根据host， 3306 也许是TCP连接的端口号，此时本服务器就作为client连接数据库所在
    mysql = mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str(), db_name.c_str(), 3306, NULL, 0);
    if(mysql == NULL)
    {
        cout << "Error: " << mysql_error(mysql);
        exit(1);
    }
    if (mysql_query(mysql, "set names utf8;") != 0)
    {
        cout << "mysql_query error" << endl;
    }
    return true;
}

//暂定：未使用
bool ChatDataBase::exeSQL(string sql) {
    if (mysql_query(mysql,sql.c_str()))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return false;
    }
    else // 查询成功
    {
        result = mysql_store_result(mysql);  //获取结果集
        if (result)  // 返回了结果集
        {
            int  num_fields = mysql_num_fields(result);   //获取结果集中总共的字段数，即列数
            int  num_rows=mysql_num_rows(result);       //获取结果集中总共的行数
            for(int i=0;i<num_rows;i++) //输出每一行
            {
                //获取下一行数据
                row=mysql_fetch_row(result);
                if(row<0) break;

                for(int j=0;j<num_fields;j++)  //输出每一字段
                {
                    cout<<row[j]<<"\t\t";
                }
                cout<<endl;
            }

        }
        else  // result==NULL
        {
            if(mysql_field_count(mysql) == 0)   //代表执行的是update,insert,delete类的非查询语句
            {
                // (it was not a SELECT)
                int num_rows = mysql_affected_rows(mysql);  //返回update,insert,delete影响的行数
                printf("%s execute successed! affect %d rows\n", sql.c_str(), num_rows);
            }
            else // error
            {
                cout<<"Get result error: "<<mysql_error(mysql);
                return false;
            }
        }
    }
    return true;
}

//12.
//查找在T_FRIEND_NOTIFICATION中有和 userId 绑定的那个ID在T_USER里的信息，尾添加到传入的vector中
void ChatDataBase::get_all_add_friend_notification(int userId, vector<User> &users) {
    char sql[1024] = {0};
    //START TRANSACTION语句启动一个事务。
    mysql_query(mysql, "start transaction");
    //查找在T_FRIEND_NOTIFICATION中有和 userId 绑定的那个ID，在T_USER里的信息
    sprintf(sql, "SELECT T_USER.f_user_id, T_USER.f_user_name, T_USER.f_online, \
            T_USER.f_photo, T_USER.f_signature from T_FRIEND_NOTIFICATION, T_USER WHERE \
            T_FRIEND_NOTIFICATION.f_user_id1 = T_USER.f_user_id AND T_FRIEND_NOTIFICATION.f_user_id2 = '%d';", userId);
    if (mysql_query(mysql, sql)) {
        fprintf(stderr, "select add friend notification failed\n");
        mysql_query(mysql,"rollback");
        return;
    }
    //返回结果集地址
    MYSQL_RES *res = mysql_store_result(mysql);
    MYSQL_ROW row;
    //返回结果集字段数
    auto numField = mysql_num_fields(res);
    //获取结果集内容，且分类存入User结构体中
    while(row = mysql_fetch_row(res))
    {
        User t;
        for (int i = 0; i < numField; i++) {
            if (i == 0) {
                t.userId = stoi(row[i]);
            } else if (i == 1) {
                t.userName = row[i];
            } else if (i == 2) {
                t.online = stoi(row[i]);
            } else if (i == 3) {
                t.iconStr = row[i];
            } else if (i == 4) {
                t.desc = row[i];
            }
        }
        //尾添加元素User 到传入的vector容器中
        users.push_back(t);
    }
    memset(sql, 0, sizeof(sql));
    //删除T_FRIEND_NOTIFICATION表中 f_user_id2 为 userId 的那一行
    sprintf(sql, "DELETE FROM T_FRIEND_NOTIFICATION WHERE T_FRIEND_NOTIFICATION.f_user_id2 = '%d';", userId);
    if (mysql_query(mysql, sql)) {
        fprintf(stderr, "delete friend notification failed\n");
        mysql_query(mysql,"rollback");
        return;
    }
    //提交事务
    if(mysql_query(mysql,"commit"))
    {
        fprintf(stderr,"mysql commit failure.\n");
    }
    return;
}

//11.
//俩ID插入T_USER_RELATIONSHIP表
void ChatDataBase::my_data_base_add_friend(int sendId, int receiveId) {
    char sql[256] = {0};
    //俩ID插入T_USER_RELATIONSHIP表。但是顺序现在是sendId在前
    sprintf(sql, "INSERT INTO T_USER_RELATIONSHIP VALUES ('%d', '%d', '0');", sendId, receiveId);
    if (mysql_query(mysql, sql)) {
        cout << "insert friend relationship error" << endl;
        return ;
    }
    //sql清空
    memset(sql, 0, sizeof(sql));
    //俩ID插入T_USER_RELATIONSHIP表，现在让receiveId在前
    sprintf(sql, "INSERT INTO T_USER_RELATIONSHIP VALUES ('%d', '%d', '0');", receiveId, sendId);
    if (mysql_query(mysql, sql)) {
        cout << "insert friend relationship error" << endl;
        return ;
    }
}

//暂定：未使用
void ChatDataBase::my_database_user_add_group_notification(int user_id,int group_id)
{
    if(!my_database_group_id_exist(group_id)) return;
    char sql[1024] = {0};
    sprintf(sql, "select f_group_owner_id from T_GROUP where f_group_id='%d';", group_id);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
    }
    else
    {
        cout << "mysql_query success" << endl;
    }
    MYSQL_RES *res = mysql_store_result(mysql);
    MYSQL_ROW row = mysql_fetch_row(res);
    string temp_group_owner_id = row[0];
    cout<<temp_group_owner_id<<endl;//群主 的id
    memset(sql, 0, sizeof(sql));
    sprintf(sql, "insert into T_GROUP_NOTIFICATION(f_group_owner_id,f_user_id,f_type,f_group_id)values('%d','%d','%s','%d');", std::stoi(temp_group_owner_id), user_id,"add",group_id);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql insert notification error" << endl;
        return ;
    }
    else
    {
        cout << "mysql insert notification success" << endl;
    }
}

//14.2  //15.2
//通过群Id查找群：T_GROUP中查找 组ID为groupid 的组相关信息，并存入Group结构体
void ChatDataBase::my_database_search_group(Group &gp, int groupid) {
    char sql[1024] = {0};
    //T_GROUP中查找 组ID为groupid 的组相关信息
    sprintf(sql, "select * from T_GROUP where T_GROUP.f_group_id='%d'", groupid);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //返回结果集地址、字段数。
    auto res = mysql_store_result(mysql);
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    //获取结果集内容，分类存入传入的Group结构体
    while((row=mysql_fetch_row(res))!=NULL)
    {
        for(int i=0;i<numField;i++)
        {
            if(i==0)
                gp.groupId = stoi(row[0]);
            if(i==1)
                gp.name=row[1];
            if(i==2)
                gp.ownerId = stoi(row[2]);
        }
    }
}

//15.1
//用户加入群聊    T_GROUP_MEMBER中插入俩数据：组ID、成员ID
void ChatDataBase::my_database_user_add_group(int groupid, int userid) {
    char sql[128];
    memset(sql,0,sizeof(sql));
    //T_GROUP_MEMBER中插入俩数据：组ID、成员ID
    sprintf(sql,"insert into T_GROUP_MEMBER(f_group_id,f_user_id) values('%d','%d')",groupid,userid);
    if(mysql_query(mysql,sql) !=0)
    {
        cout<<"insert failed"<<endl;
    }
}

//13.
//创建群聊：表T_GROUP插入俩数据：群聊名称，群聊拥有者ID（组ID用了主键自增长，不需要额外传递）  群聊成员增加：T_GROUP_MEMBER表中插入俩数据：组ID、拥有者ID   
void ChatDataBase::my_database_add_new_group(string group_name, int ownerid,int &group_id)
{
    char sql[128] = {0};
    //表T_GROUP插入俩数据：群聊名称，群聊拥有者ID  （群聊ID在表中用了主键自增长，不需要额外传递）
    sprintf(sql, "insert into T_GROUP(f_group_name,f_group_owner_id) values('%s','%d');", group_name.c_str(),ownerid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query insert error" << endl;
    }
    memset(sql, 0, sizeof(sql));
    //获取T_GROUP表中末尾的f_group_id  主键自增长，所以末尾的那个就是刚插入数据的那一行
    sprintf(sql, "select max(f_group_id) from T_GROUP;");
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
    }
    //获取结果集地址、再获取结果集内容，存入row，
    MYSQL_RES *res = mysql_store_result(mysql);
    MYSQL_ROW row = mysql_fetch_row(res);
    cout<<row[0]<<endl;
    string temp = row[0];               //f_group_id 组ID 存入temp
    group_id=stoi(temp);                //传入的变量记录组ID
    memset(sql, 0, sizeof(sql));
    //T_GROUP_MEMBER表中插入俩数据：组ID、拥有者ID  此表是：群聊成员 
    sprintf(sql, "insert into T_GROUP_MEMBER(f_group_id,f_user_id) values('%d','%d');", std::stoi(temp),ownerid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query insert error" << endl;
    }
}

//17.1  //18.2
//查询群groupid中 所有成员的 成员ID，将这些成员ID的用户信息，存入传进来的vector容器
void ChatDataBase::my_database_get_group_user(int groupid, vector<User> &ur) {
    char sql[1024] = {0};
    //查询T_GROUP_MEMBER中，组ID为groupid 的行的 成员ID（f_user_id） 重复的结果用DISTINCT消除
    //查询群groupid中 所有成员的 成员ID
    sprintf(sql, "select DISTINCT(f_user_id) from T_GROUP_MEMBER where f_group_id ='%d';", groupid);//DISTINCT消除结果集中的重复行。
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    vector<int> temp;
    //获取结果集地址、字段数
    auto res = mysql_store_result(mysql);
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    //获取结果集内容，并将获取的 成员ID 尾添加到temp（可能有多个，所以用循环）
    while((row=mysql_fetch_row(res))!=NULL)
    {
        //将获取的 成员ID 尾添加到temp
        temp.push_back(stoi(row[0]));
    }
    User ur_temp;
    vector<int>::iterator iter=temp.begin();
    //迭代器遍历，获取成员ID的用户信息，存入ur容器
    while(iter!=temp.end()){
        //查询数据库内，当前遍历到的成员ID用户信息，将用户信息存入User结构体ur_temp
        my_database_user_information(ur_temp,*iter);
        ur.push_back(ur_temp);  //User尾添加到 ur容器（引用的变量）
        iter++;
    }
}

//17.2
//获取 群ID为groupid 的群里所有聊天消息的信息，存进传入的ms容器，再清除userid在群中的未读消息数
void ChatDataBase::my_database_get_group_chat_msg(int groupid,int userid,vector<Message> &ms) {
    char sql[1024] = {0};
    //查询T_GROUP_MESSAGE中组ID为groupid 的所有行的信息（就是获取 群groupid，中所有聊天消息的信息）
    sprintf(sql, "select * from T_GROUP_MESSAGE where f_groupid='%d'", groupid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
        return;
    }
    //获取结果集地址、字段数
    auto res = mysql_store_result(mysql);
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    Message temp;
    //获取结果集内容，分类存入temp，temp再尾添加到传入的ms容器
    while((row=mysql_fetch_row(res))!=NULL)
    {
        for(int i=0;i<numField;i++) {
            if(i==1){temp.sendId=stoi(row[i]);}
            if(i==2){temp.receiveId=stoi(row[i]);}
            if(i==3){temp.content=row[i];}
            if(i==4){temp.time=row[i];}
            if(i==5){temp.type=stoi(row[i]);}
        }
        ms.push_back(temp);
    }
    //更新T_GROUP_MEMBER，让组ID、成员ID为groupid,userid 的行 f_msg_num归0（聊天消息数清0）
    memset(sql,0,sizeof(sql));
    sprintf(sql,"update T_GROUP_MEMBER SET f_msg_num=0 WHERE f_group_id='%d' and f_user_id='%d';",groupid,userid);
    if (mysql_query(mysql, sql) != 0)
    {
        cout << "mysql_query error" << endl;
        return;
    }
}

//18.1
//在群消息记录中一条消息，如果用户没上线，增加其离线消息数
//所有在该群中的 离线成员ID 在表T_GROUP_MEMBER 中的未读群聊消息数（+1）
void ChatDataBase::my_database_group_msg_insert(Message ms) {
    char sql[1024] = {0};
    //T_GROUP_MESSAGE插入数据：发送者ID、接收者ID（群ID）、聊天内容的地址、时间的地址、类型
    sprintf(sql, "INSERT into T_GROUP_MESSAGE(f_senderid,f_groupid,f_msgcontent,f_sendtime,f_type) VALUES('%d','%d','%s','%s','%d');", ms.sendId,ms.receiveId,ms.content.c_str(),ms.time.c_str(),ms.type);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //T_USER 和 T_GROUP_MEMBER 中查询所有相关的 f_user_id（用户ID），条件是，发送者为sendId，接收者为receiveId（群ID），成员在离线状态
    vector<int> ur_temp;
    memset(sql,0,sizeof(sql));
    sprintf(sql,"select T_USER.f_user_id from T_USER,T_GROUP_MEMBER where T_USER.f_user_id=T_GROUP_MEMBER.f_user_id \n"
                "and T_GROUP_MEMBER.f_group_id='%d' and T_USER.f_online=0 and T_USER.f_user_id!='%d';",ms.receiveId,ms.sendId);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //获取结果集地址、内容
    auto res = mysql_store_result(mysql);
    MYSQL_ROW row;
    //查到的所有 用户ID 依次尾添加到ur_temp
    while((row=mysql_fetch_row(res))!=NULL)
    {
        ur_temp.push_back(stoi(row[0]));
    }
    //迭代器遍历，更新发送消息的 成员ID在T_GROUP_MEMBER 中的群聊消息数（+1）
    vector<int>::iterator iter = ur_temp.begin();
    while(iter!=ur_temp.end())
    {
        //更新T_GROUP_MEMBER，当前遍历到的 离线成员ID，在该群中的 未读群聊消息数+1
        memset(sql,0,sizeof(sql));
        sprintf(sql,"UPDATE T_GROUP_MEMBER SET f_msg_num=f_msg_num+1 where f_group_id='%d' and f_user_id='%d';",ms.receiveId,*iter);
        if (mysql_query(mysql,sql))
        {
            cout<<"Query Error: "<<mysql_error(mysql);
            return ;
        }
        iter++;
    }
}

//16.
//通过一个用户id获取该用户加入的所有群聊，并获取相应的历史消息数、群ID、群名、群拥有者ID
void ChatDataBase::my_database_get_group(vector<Group> &vgp, vector<int> &group_message, int userid) {
    char sql[1024] = {0};
    //查找 成员ID为userid 在两张表里的信息（T_GROUP、T_GROUP_MEMBER）
    sprintf(sql, "select T_GROUP.f_group_id,T_GROUP.f_group_name,T_GROUP.f_group_owner_id,T_GROUP_MEMBER.f_msg_num from T_GROUP_MEMBER,T_GROUP where T_GROUP.f_group_id = T_GROUP_MEMBER.f_group_id and f_user_id='%d';",userid);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
        return ;
    }
    //获取结果集地址、字段数
    auto res = mysql_store_result(mysql);
    auto numField = mysql_num_fields(res);
    MYSQL_ROW row;
    //获取结果集内容，并分类存入Group，再将Group尾添加到传入的vgp容器中
    //且f_msg_num（历史消息数）尾添加到传入的group_message容器中
    while((row=mysql_fetch_row(res))!=NULL)
    {
        Group temp;
        for(int i=0;i<numField;i++)
        {
            if(i==0)
                temp.groupId=stoi(row[0]);          //f_group_id
            if(i==1)
                temp.name=row[1];                   //f_group_name
            if(i==2)
                temp.ownerId=stoi(row[2]);          //f_group_owner_id
        }
        vgp.push_back(temp);
        group_message.push_back(stoi(row[3]));
    }
}

//14.3
//T_GROUP_MEMBER中查找 组id为groupId 并且 成员id为userId 的那一行信息存不存在，成功查到了返回true
bool ChatDataBase::my_database_in_group(int userId, int groupId) {
    char sql[256] = {0};
    //T_GROUP_MEMBER中查找 组id为groupId 并且 成员id为userId 的那一行信息
    sprintf(sql, "SELECT * FROM T_GROUP_MEMBER WHERE T_GROUP_MEMBER.f_group_id = '%d' AND T_GROUP_MEMBER.f_user_id = '%d';", groupId, userId);
    if (mysql_query(mysql,sql))
    {
        cout<<"Query Error: "<<mysql_error(mysql);
    }
    //返回结果集地址、字段数。
    auto res = mysql_store_result(mysql);
    int  num_rows = mysql_num_rows(res); 
    //成功查到
    if (num_rows) {  //已经在群里
        return true;
    } else { //不再群里
    //没查到
        return false;
    }
}