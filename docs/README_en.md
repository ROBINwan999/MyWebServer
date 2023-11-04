# project introduction

Webserver is a small http server written in C/C++ when I was learning network programming under Linux, for practice and to help me deepen my understanding of multithreading, network communication and server programs.

# project start

## Linux platform, GCC compiler

You need to modify the root_doc in http_connection.cpp to your path

After the modification is completed, cd into the webserver directory

```
g++ *.cpp -pthread
./a.out port
```

For example: specify the port number 8888

```
./a.out 8888
```

Then, open a browser, such as chrome, and enter the URL to access the server

```
http://IPaddress:port/index.html
```

For example：IP: 192.168.0.1

```
http://192.168.0.1:8888/index.html
```

## What each function does

HttpConnection.h

```c++
void init(int sockfd, const sockaddr_in &addr); // init new connection
void closeConnection();                         // close connection
void process();                                 // process client request
bool read();                                    // non-blocking read
bool write();                                   // non-blocking write
```

ThreadPool.h

```c++
ThreadPool(int thread_number = 8, int max_request = 10000);	//create thread and detach
~ThreadPool();            //destroy thread
bool addTask(T *task);    //add new task
```

Locker.h

```c++
//mutex class
Locker();    //create mutex
~Locker();   //destroy mutex
bool lock();    //lock
bool unlock();  //unlock
pthread_mutex_t *getMutex(); 	//get mutex
//condition variable class
ConditionVariable();    //create condition variable
~ConditionVariable();   //destroy condition variable
bool wait(pthread_mutex_t *mutex);    //Block thread waiting to wake up
bool timeWait(pthread_mutex_t *mutex, struct timespec time);	//Blocking thread waiting time to wake up
bool signal();     //wake up 1+ threads
bool broadCast();  //wake up all threads
//semaphore class
Semaphore();    //the default constructed set semaphore to 0
Semaphore(int num); //construct the specified semaphore with parameters
~Semaphore();   //destroy semaphore
bool wait();    //block semaphore
bool post();    //add semaphore
```

main.cpp

```c++
extern void addfd(int epollfd, int fd, bool one_shot);	//add file descriptor
extern void removefd(int epollfd, int fd);    //remove file descriptor
void addSignal(int sig, void(handler)(int));  //add signal
int main(int argc, char *argv[]);    //main thread process IO
```

Suggested reading order of source code: Locker -> ThreadPool -> HttpConnection -> main

# pressure test

test tool webbench

test method: 

Run the program first and compile it according to the above method. Then open a new terminal, enter the test_pressure/webbench-1.5/ directory, enter make to compile the Makefile, if there is no make, you can first output sudo apt install make to download, and then enter:

```
./webbench -c numberOfClient -t eachClientStaySec http://IPaddress:port/index.html
```

For example, test 1000 client accesses, each for 5 seconds, with IP 192.168.198.128 and port 8888

```
./webbench -c 1000 -t 5 http://192.168.198.128:8888/index.html
```

The test results are as follows: (passed)

![](/image/1000-5.png)

Increase the pressure, this time test 5000 customer visits, each stay for 30 seconds

```
./webbench -c 5000 -t 30 http://192.168.198.128:8888/index.html
```

The test results are as follows: (passed)

![](/image/5000-30.png)
