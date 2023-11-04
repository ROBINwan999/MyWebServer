#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>

#include <exception>

// 线程同步机制封装类

// 封装一个互斥锁类，利用RAII来自动管理资源
class Locker {
   public:
    Locker();  // 使用构造创建互斥锁

    ~Locker();  // 使用析构释放互斥锁

    bool lock();  // 上锁

    bool unlock();  // 解锁

    pthread_mutex_t *getMutex();  // 互斥锁的getter

   private:
    pthread_mutex_t mutex_;
};

// 封装一个条件变量类，利用RAII来自动管理资源
class ConditionVariable {
   public:
    ConditionVariable();  // 使用构造来创建条件变量

    ~ConditionVariable();  // 使用析构来销毁条件变量

    // 在condition_variable_上阻塞线程，加入condition_variable_的等待队列，并释放互斥锁mutex。
    // 如果其他线程使用同一个条件变量，调用了signal/broadcast，唤醒线程会重新获得mutex
    bool wait(pthread_mutex_t *mutex);

    // 与wait类似，但是该线程的唤醒条件是调用了signal/braodcast，或者系统时间达到了abstime
    bool timeWait(pthread_mutex_t *mutex, struct timespec time);

    bool signal();  // 唤醒在condition_variable_上等待的至少一个线程

    bool broadCast();  // 唤醒所有在condition_variable_上等待的线程

   private:
    pthread_cond_t condition_variable_;
};

// 封装一个信号量类，利用RAII来自动管理资源
class Semaphore {
   public:
    Semaphore();  // 使用构造来创建信号量

    Semaphore(int num);  // 带参构造

    ~Semaphore();  // 使用析构来销毁信号量

    bool wait();  // 等待信号量

    bool post();  // 增加信号量

   private:
    sem_t semaphore_;
};

#endif