#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <string>
#include <iostream>
#include "user.hpp"
#include <vector>
#include "groupuser.hpp"
using namespace std;
using namespace muduo;



// 获得单例对象的接口函数
ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 祖册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG,std::bind(&ChatService::login,this,_1,_2,_3)});
    _msgHandlerMap.insert({REG_MSG,std::bind(&ChatService::reg,this,_1,_2,_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG,std::bind(&ChatService::onChat,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG,std::bind(&ChatService::addFriend,this,_1,_2,_3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG,std::bind(&ChatService::createGroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG,std::bind(&ChatService::addGroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG,std::bind(&ChatService::groupChat,this,_1,_2,_3)});
    _msgHandlerMap.insert({LOGINOUT_MSG,std::bind(&ChatService::loginout,this,_1,_2,_3)}); 
    
    // 连接redis服务器
    if(_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage,this,_1,_2)); //一个为通道号，一个为通道消息
    }
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid){
    // 记录错误日志，msgid没有对应的事件的处理回调
    auto it = _msgHandlerMap.find(msgid);
    if(it == _msgHandlerMap.end()){

        // 返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn,json &js,Timestamp){

         LOG_ERROR << "msgid:"<<msgid<<"can not find handler";
        };
    }
    else{
        return _msgHandlerMap[msgid];//绑定的方法

    }
}

// 处理业务登录
void ChatService::login(const TcpConnectionPtr &conn,json &js,Timestamp time)
{
    int id = js["id"];
    string pwd = js["password"];
    User user = _userModel.query(id);
    if(user.getId() == id && user.getPwd() == pwd){
        if(user.getState() == "online")
        {
            // 该哟偶农户已经登录，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["error"] = 2;
            response["errmsg"] = "该账户已经登录，请重新输入新账号";
            conn->send(response.dump());
        }
        else{

            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id,conn}); // 用户的上线和下线会导致线程安全，需要注意
            } // lock_guard 对其作用域进行加锁，作用域结束之后进行解锁，实现insert的线程安全
            
            //id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id);


            // 登录成功，更新用户状态信息
            user.setState("online");
            _userModel.update(user);
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["error"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 如果有离线用户信息就进行返回
            vector<string> offlinemsg = _offlineMsgModel.query(user.getId());
            response["offlinemsg"] = offlinemsg;
            _offlineMsgModel.remove(user.getId()); // 有离线消息，则把他读取/
            
            // 登录成功后，显示所有好友信息
            vector<User> listFriend = _friendModel.query(user.getId());
            if(!listFriend.empty()){
                vector<string> vec2;
                for(User &user:listFriend)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            
            }
             
            //登录成功后，显示所有群组消息
            vector<Group> listGroups = _groupModel.queryGroups(user.getId());
            if(!listGroups.empty()){
                vector<string> resGroups;
                
                for(Group &group : listGroups){
                    json gjs;
                    cout<<group.getId();
                    gjs["id"] = group.getId();
                    gjs["groupname"] = group.getName();
                    gjs["groupdesc"] = group.getDesc();
                    
                    vector<string> tempUserlist;
                    for(GroupUser &gs : group.getUsers())
                    {
                        json gsjs ;
                        gsjs["id"] = gs.getId();
                        gsjs["name"] = gs.getName();
                        gsjs["state"] = gs.getState();
                        gsjs["role"] = gs.getRole();
                        tempUserlist.push_back(gsjs.dump());
                    }
                    gjs["users"] = tempUserlist;
                    resGroups.push_back(gjs.dump());
                }
                 response["groups"] = resGroups;
            }
            // 登录成功后
            conn->send(response.dump());
        }
    }
        else{
            // 该用户不存在，用户存在但是密码错误，登录失败
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["error"] = 1;
            response["errmsg"] = "用户名或者密码错误";
            conn->send(response.dump());
        }
}

// 处理注册业务 name password
void ChatService::reg(const TcpConnectionPtr &conn,json &js,Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if(state){
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["id"] = user.getId();
        response["error"] = 0;
        conn->send(response.dump());
    }
    else{
        // 注册失败 
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["error"] = 1;
        conn->send(response.dump());
    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn,json &js,Timestamp time)
{   int userid = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if(it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        
        }
    
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    User user = _userModel.query(userid);
    user.setState("offline");
    cout<<"chaicive.loginout"<<endl;
    _userModel.update(user);
} 
// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        
        lock_guard<mutex> lock(_connMutex);// 结束只有自动释放锁

        for(auto it=_userConnMap.begin(); it != _userConnMap.end(); ++it){
            if(it->second == conn){
                // 从map表删除用户的链接信息
                user = _userModel.query(it->first);
                user.setState("offline");
                _userModel.update(user);
                _userConnMap.erase(it);
                break;
            }   
        }

    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());
}

// 一对一聊天
void ChatService::onChat(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int toid = js["to"].get<int>(); // 装成整型
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if(it != _userConnMap.end())
        {
            // toid在线，转发消息
            it->second->send(js.dump());
            return;
        }
    }

    // 查询toid是否在线
    User user = _userModel.query(toid);
    if(user.getState() == "online")
    {
        // 查询数据库表明在线
        _redis.publish(toid,js.dump());
        return;
    }
    
    // 表示发送的是离校消息,存到离线消息里面
    _offlineMsgModel.insert(toid,js.dump());
}

// 服务器异常，重置业务
void ChatService::reset(){
    // 把online状态转换为offline
    _userModel.resetState();
}
void ChatService::addFriend(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid,friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js,Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1,name,desc);
    if(_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid,group.getId(),"creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn,json &js,Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid,groupid,"normal");
}

// 群组聊天服务
void ChatService::groupChat(const TcpConnectionPtr &conn,json &js,Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    cout<<"groupid"<<groupid<<endl;
    vector<int> useridVec = _groupModel.queryGroupUsers(userid,groupid);
    for(int id : useridVec)
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(id);
        if(it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else{
            // 查询toid是否在线
            User user = _userModel.query(id);
            if(user.getState() == "online")
            {
                // 在这个通道号发送消息  
                _redis.publish(id,js.dump());
            }
            else{

                // 存取离线消息
                _offlineMsgModel.insert(id,js.dump());
            }
        }
    }
}
 
// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid,string msg)
{
    json js = json::parse(msg.c_str());

    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if(it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid,msg);
}


