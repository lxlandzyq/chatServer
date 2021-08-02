 
# 使用
```
mkdir build

cd build

cmake ..

make

```

此时在项目目录中会出现一个bin文件夹
```
cd bin

./ChatServer 127.0.0.1 6000   (服务端1运行)

./ChatServer 127.0.0.1 6002   (服务端2运行)


```
客户端运行
```
./ChatClient 127.0.0.1 8000
./ChatClient 127.0.0.1 8000
```

