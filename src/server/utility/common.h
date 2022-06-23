#ifndef COMMON_H__
#define COMMON_H__
#include <string>
#include "../server/json/json.h"

using namespace std;

//用户信息 结构体
struct User{
    int userId;
    string userName;
    string iconStr;
    string desc;
    int online;
};

//消息 结构体
struct Message {
    int sendId;
    int receiveId;
    int type;
    string content;//这里只是string，考虑如何传输图片和其他的数据
    string time;
};

//群聊 结构体
struct Group {
    int ownerId;
    int groupId;
    string name;
    Group(int own = 0, int gid = 0, string _name = ""): ownerId(own), groupId(gid), name(_name) {
        
    }
};


Json::Value userToJsonObj(User user);
Json::Value messageToJsonObj(Message msg);
Json::Value groupToJsonObj(const Group &g);


#endif