#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

#include <cstdio>
#include <list>

#include "http_connection.h"
#include "locker.h"

// 线程池类，定义模板类是为了代码的复用，模板参数T就是任务类
template <typename T>
class ThreadPool {
   public:
    ThreadPool(int thread_number = 8, int max_request = 10000);

    ~ThreadPool();

    bool addTask(T *task);

   private:
    static void *worker(void *arg);

    void run();

   private:
    // 线程的数量
    int thread_number_;

    // 线程池数组，大小为thread_number_
    pthread_t *threads_;

    // 请求队列中最多允许的，等待处理的请求数量
    int max_request_;

    // 请求队列
    std::list<T *> work_queue_;

    // 互斥锁
    Locker queue_locker_;

    // 信号量来判断是否有任务需要处理
    Semaphore queue_status_;

    // 是否结束线程
    bool stop_;
};

template <typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_request)
    : thread_number_(thread_number), max_request_(max_request), stop_(false), threads_(NULL) {
    if (thread_number <= 0 || max_request <= 0) {
        throw std::exception();
    }

    threads_ = new pthread_t[thread_number_];
    if (!threads_) {
        throw std::exception();
    }

    // 创建thread_number_个线程，并将他们设置为线程脱离
    for (int i = 0; i < thread_number_; ++i) {
        printf("正在创建第%d个线程\n", i);

        if ((pthread_create(threads_ + i, NULL, worker, this)) != 0) {
            delete[] threads_;
            throw std::exception();
        }

        // 创建成功，设置线程分离
        if (pthread_detach(threads_[i]) != 0) {
            delete[] threads_;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] threads_;
    stop_ = true;
}

template <typename T>
bool ThreadPool<T>::addTask(T *task) {
    queue_locker_.lock();
    if (work_queue_.size() > max_request_) {
        queue_locker_.unlock();
        return false;
    }

    work_queue_.push_back(task);
    queue_locker_.unlock();
    queue_status_.post();

    return true;
}

template <typename T>
void *ThreadPool<T>::worker(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();

    return pool;
}

template <typename T>
void ThreadPool<T>::run() {
    while (!stop_) {
        // 判断有没有任务去执行，如果信号量有值就不阻塞，没有值就阻塞在这
        queue_status_.wait();
        // 到这里说明有任务，上锁，开始执行
        queue_locker_.lock();
        if (work_queue_.empty()) {
            queue_locker_.unlock();
            continue;
        }
        // 到这里说明有数据
        T *task = work_queue_.front();
        work_queue_.pop_front();
        queue_locker_.unlock();

        if (!task) {
            continue;
        }

        task->process();
    }
}

#endif