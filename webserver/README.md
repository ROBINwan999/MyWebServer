# 关于本项目

Webserver是我在学习C++ 网络编程的时候编写的一个小型http服务器，用于实践网络编程，帮助我加深对服务器程序的理解。

# 编译

## Linux平台，GCC编译器

进入webserver目录下

```
g++ *.cpp -pthread
./a.out 端口号
```

例如：指定端口号8888

```
./a.out 8888
```

然后，打开浏览器，比如chrome，输入网址访问服务器

```
http://IP地址:端口号/index.html
```

例如：IP: 192.168.0.1

```
http://192.168.0.1:8888/index.html
```

## 每个函数的作用

HttpConnection.h

```c++
void init(int sockfd, const sockaddr_in &addr); // 初始化新接受的连接
void closeConnection();                         // 关闭连接
void process();                                 // 处理客户端请求
bool read();                                    // 非阻塞读
bool write();                                   // 非阻塞写
```

ThreadPool.h

```c++
ThreadPool(int thread_number = 8, int max_request = 10000);	//创建线程并分离
~ThreadPool();																							//销毁线程
bool addTask(T *task);																			//添加任务
```

Locker.h

```c++
//互斥锁类
Locker(); 										//创建互斥锁
~Locker(); 										//销毁互斥锁
bool lock(); 									//上锁
bool unlock(); 								//解锁
pthread_mutex_t *getMutex(); 	//获得互斥锁
//条件变量类
ConditionVariable(); 																					//创建条件变量
~ConditionVariable(); 																				//销毁条件变量
bool wait(pthread_mutex_t *mutex);														//阻塞线程等待唤醒
bool timeWait(pthread_mutex_t *mutex, struct timespec time);	//阻塞线程等待时间唤醒
bool signal(); 																								//唤醒1+个线程
bool broadCast(); 																						//唤醒所有线程
//信号量类
Semaphore(); 				//缺省构造信号量为0
Semaphore(int num); //带参构造指定信号量
~Semaphore(); 			//销毁信号量
bool wait(); 				//阻塞信号量
bool post(); 				//增加信号量
```

main.cpp

```c++
extern void addfd(int epollfd, int fd, bool one_shot);	//添加文件描述符
extern void removefd(int epollfd, int fd);							//删除文件描述符
void addSignal(int sig, void(handler)(int));						//添加信号量
int main(int argc, char *argv[]);												//主线程处理IO
```

建议源码阅读顺序: Locker -> ThreadPool -> HttpConnection -> main

## 工作流程





## 压力测试

测试工具webbench

测试方式：

先运行程序，按照上述编译方法。然后新开启一个终端，进入test_pressure/webbench-1.5/目录下，输入make编译Makefile，如果没有make可以先输出sudo apt install make下载，然后输入：

```
./webbench -c 测试访问的客户数量 -t 每个客户停留的时间 http://IP地址:端口号/index.html
```

例如，测试1000个客户访问，每个停留5秒钟，IP为192.168.198.128，端口为8888

```
./webbench -c 1000 -t 5 http://192.168.198.128:8888/index.html
```

测试结果如下：（通过）

![image-20220709033055412](/Users/robin/Library/Application Support/typora-user-images/image-20220709033055412.png)

加大压力，这次测试5000个客户访问，每个停留30秒种

```
./webbench -c 5000 -t 30 http://192.168.198.128:8888/index.html
```

测试结果如下：（通过）

![image-20220709033537260](/Users/robin/Library/Application Support/typora-user-images/image-20220709033537260.png)