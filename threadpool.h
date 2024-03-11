#pragma once
#include "task.h"
#include <pthread.h>
#include <queue>
#include <unordered_set>
#define DEFAULT_THREAD_NUM 10
#define THREAD_NUM(N) (N > 1000 || N < 1) ? N : DEFAULT_THREAD_NUM

// 线程池是一个生产者-消费者模型，其中管理者线程充当生产者，而加入的任务
// 相当于消费者
// The thead pool is a model of productor-consumer, the manager thread is productor
//  and the joined tasks are the consumers
class ThreadPool
{

private:
    std::queue<Task> *task_queue; // 任务队列

    std::unordered_set<int> *unordered_addr_set; // 用于添加任务时任务判断任务该任务是否重复添加

    pthread_t *worker_threads; // 工作的线程(消费者)

    pthread_t manager_thread; // 管理者线程，当有任务时创建线程(消费者)执行，当消费者线程过多时销毁消费者

    pthread_mutex_t mutex_pool; // 线程池互斥锁, 当多个线程操作多个成员变量时使用

    pthread_mutex_t mutex_busy_thread; // 忙碌的线程数互斥锁，仅多个线程同时操作忙碌的线程数使用

    size_t task_size; // 任务队列长度

    int max_thread_num; // 最大线程数

    int min_thread_num; // 最小线程数

    int busy_thrad_num; // 当前正在工作的线程数

    int alive_thread_num; // 存活的线程数

    int exit_thread_num; // 当前需要销毁的线程数

    pthread_cond_t cond_queue_empty; // 当任务队列空时阻塞

    pthread_cond_t cond_queue_full; // 当任务队列满时阻塞

    bool shutdown; // 线程池是否已销毁

protected:
    // 创建线程池
    void pool_create(int max_thread, int min_thread, size_t qsize);

    // 销毁线程池
    void pool_destroy();

    // 消费者线程的处理，声明为友元函数,使其能在类外调用，作为线程的入口函数
    friend void *working(void *arg);

    // 线程退出
    friend void thread_exit(pthread_t *threads, int _n);

    // 管理者线程
    friend void *manager(void *arg);

public:
    ThreadPool(int max_thread, int min_thread, size_t qsize);

    ~ThreadPool();

    // 向线程池中添加任务
    void put_task(void (*entrance)(void *arg), void *arg, int function_key);
};

typedef ThreadPool *pool;