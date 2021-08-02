#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

// user表的数据操作类
class UserModel{
public:
    bool insert(User &user);

    User query(int id);
    
    bool update(User user);
    
    void resetState();// 全部online重置为offline
};

#endif


