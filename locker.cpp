#include "locker.h"

//使用构造创建互斥锁
Locker::Locker()
{
    if (pthread_mutex_init(&mutex_, nullptr) != 0)
    {
        throw std::exception();
    }
}

//使用析构释放互斥锁
Locker::~Locker()
{
    pthread_mutex_destroy(&mutex_);
}

//上锁
bool Locker::lock()
{
    return pthread_mutex_lock(&mutex_) == 0;
}

//解锁
bool Locker::unlock()
{
    return pthread_mutex_unlock(&mutex_) == 0;
}

//互斥锁的getter
pthread_mutex_t *Locker::getMutex()
{
    return &mutex_;
}

//使用构造来创建条件变量
ConditionVariable::ConditionVariable()
{
    if (pthread_cond_init(&condition_variable_, NULL) != 0)
    {
        throw std::exception();
    }
}

//使用析构来销毁条件变量
ConditionVariable::~ConditionVariable()
{
    pthread_cond_destroy(&condition_variable_);
}

//在condition_variable_上阻塞线程，加入condition_variable_的等待队列，并释放互斥锁mutex。
//如果其他线程使用同一个条件变量，调用了signal/broadcast，唤醒线程会重新获得mutex
bool ConditionVariable::wait(pthread_mutex_t *mutex)
{
    return pthread_cond_wait(&condition_variable_, mutex) == 0;
}

//与wait类似，但是该线程的唤醒条件是调用了signal/braodcast，或者系统时间达到了abstime
bool ConditionVariable::timeWait(pthread_mutex_t *mutex, struct timespec time)
{
    return pthread_cond_timedwait(&condition_variable_, mutex, &time) == 0;
}

//唤醒在condition_variable_上等待的至少一个线程
bool ConditionVariable::signal()
{
    return pthread_cond_signal(&condition_variable_) == 0;
}

//唤醒所有在condition_variable_上等待的线程
bool ConditionVariable::broadCast()
{
    return pthread_cond_broadcast(&condition_variable_) == 0;
}

//使用构造来创建信号量
Semaphore::Semaphore()
{
    if (sem_init(&semaphore_, 0, 0) != 0)
    {
        throw std::exception();
    }
}

//带参构造
Semaphore::Semaphore(int num)
{
    if (sem_init(&semaphore_, 0, num) != 0)
    {
        throw std::exception();
    }
}

//使用析构来销毁信号量
Semaphore::~Semaphore()
{
    sem_destroy(&semaphore_);
}

//等待信号量
bool Semaphore::wait()
{
    return sem_wait(&semaphore_) == 0;
}

//增加信号量
bool Semaphore::post()
{
    return sem_post(&semaphore_) == 0;
}