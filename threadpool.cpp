#include "threadpool.h"
#include <iostream>
#include <unistd.h>
#define COUNT 2

void *working(void *arg)
{

    void thread_exit(pthread_t * threads, int _n);
    pool p = (pool)arg;
    Task task;
    while (1)
    {
        // 循环从任务队列中取出任务并执行
        pthread_mutex_lock(&p->mutex_pool);
        while (!p->shutdown && p->task_queue->empty())
        {
            pthread_cond_wait(&p->cond_queue_empty, &p->mutex_pool);

            // todo: 如果有需要销毁的线程，在此处退出
            if (p->exit_thread_num > 0)
            {
                --p->exit_thread_num;
                --p->alive_thread_num;
                pthread_mutex_unlock(&p->mutex_pool);
                thread_exit(p->worker_threads, p->max_thread_num);
            }
        }

        if (p->shutdown)
        {
            // 如果线程池已经关闭了，解锁线程池并退出当前线程
            pthread_mutex_unlock(&p->mutex_pool);
            thread_exit(p->worker_threads, p->max_thread_num);
        }

        pthread_cond_signal(&p->cond_queue_full); // 任务即将被消费，唤醒因任务队列满而阻塞的生产者
        Task task = p->task_queue->front();
        p->task_queue->pop();

        pthread_mutex_unlock(&p->mutex_pool);
        pthread_mutex_lock(&p->mutex_busy_thread);
        ++p->busy_thrad_num; // 忙碌的线程+1
        pthread_mutex_unlock(&p->mutex_busy_thread);
        (*task.entrance)(task.arg); // 执行任务函数

        pthread_mutex_lock(&p->mutex_busy_thread);
        --p->busy_thrad_num; // 执行完了，忙碌的线程-1
        p->unordered_addr_set->erase(task.key);
        pthread_mutex_unlock(&p->mutex_busy_thread);
    }

    return nullptr;
}

/**
 * 管理者线程
*/
void *manager(void *arg)
{

    pool p = (pool)arg;

    while (!p->shutdown)
    {
        pthread_mutex_lock(&p->mutex_pool);
        int alive_num = p->alive_thread_num;
        int current_task_size = p->task_queue->size();
        pthread_mutex_unlock(&p->mutex_pool);

        pthread_mutex_lock(&p->mutex_busy_thread);
        int busy_num = p->busy_thrad_num;
        pthread_mutex_unlock(&p->mutex_busy_thread);

        // 添加线程：当存活的线程 < 任务队列的长度  && 存活的线程数<最大线程数
        if (alive_num < current_task_size && alive_num < p->max_thread_num)
        {
            // 每次增加2个线程
            int c = 0;
            pthread_mutex_lock(&p->mutex_pool); // 以下涉及对存活的线程进行增加操作
            for (int i = 0; c < COUNT, c < p->max_thread_num, p->alive_thread_num < p->max_thread_num; ++i)
            {
                if (p->worker_threads[i] == 0)
                {
                    pthread_create(&p->worker_threads[i], nullptr, working, p);
                    ++p->alive_thread_num;
                    ++c;
                }
            }

            pthread_mutex_unlock(&p->mutex_pool);
        }

        // 销毁线程: 当存活的线程>最小线程数 && 忙碌的线程数*2 <存活的线程数
        if (alive_num > p->min_thread_num && 2 * busy_num < alive_num)
        {
            pthread_mutex_lock(&p->mutex_pool);
            p->exit_thread_num = COUNT; // 消费者线程被唤醒后将根据此变量决定是否杀死多余的线程
            pthread_mutex_unlock(&p->mutex_pool);

            for (int i = 0; i < COUNT; i++)
            {
                pthread_cond_signal(&p->cond_queue_empty); // 唤醒因队列空而阻塞的线程，让其自杀
            }
        }

        sleep(3);
    }
    return nullptr;
}

void ThreadPool::pool_create(int max_thread, int min_thread, size_t qsize)
{
    do
    {
        this->max_thread_num = max_thread;
        this->min_thread_num = min_thread;
        this->busy_thrad_num = 0;
        this->alive_thread_num = min_thread;
        this->exit_thread_num = 0;
        this->task_queue = new std::queue<Task>;
        this->unordered_addr_set = new std::unordered_set<int>;
        this->shutdown = false;
        this->task_size = qsize;

        if (!this->task_queue)
        {
            printf("failed to create queue\n");
            break;
        }

        // 初始化互斥锁
        pthread_mutex_init(&mutex_pool, nullptr);
        pthread_mutex_init(&mutex_busy_thread, nullptr);
        pthread_cond_init(&cond_queue_empty, nullptr);
        pthread_cond_init(&cond_queue_full, nullptr);

        // 创建管理者线程
        pthread_create(&manager_thread, nullptr, manager, this);
        printf("manager thread:0x%lx\n", manager_thread);

        // 创建消费者线程
        this->worker_threads = new pthread_t(THREAD_NUM(max_thread));
        printf("workers=%p\n", this->worker_threads);
        if (!worker_threads)
        {
            printf("failed to create worker threads\n");
            break;
        }

        for (size_t i = 0; i < min_thread; ++i)
        {
            // 按最小线程数创建工作线程
            pthread_create(&worker_threads[i], nullptr, working, this);
        }

        return;

    } while (0);

    if (this->worker_threads)
    {
        delete[] this->worker_threads;
        this->worker_threads = nullptr;
    }

    if (this->task_queue)
    {
        delete this->task_queue;
        this->task_queue = nullptr;
    }
    if (this->unordered_addr_set)
    {
        delete this->unordered_addr_set;
        this->unordered_addr_set = nullptr;
    }
}

ThreadPool::ThreadPool(int max_thread, int min_thread, size_t qsize)
{

    pool_create(max_thread, min_thread, qsize);
}

ThreadPool::~ThreadPool()
{
    pool_destroy();
}

void thread_exit(pthread_t *threads, int _n)
{
    printf("thread=%p,n=%d\n", threads, _n);
    pthread_t this_thread = pthread_self();
    for (int i = 0; i < _n; ++i)
    {
        if (threads[i] == this_thread)
        {
            threads[i] = 0;
            break;
        }
    }

    printf("thread 0x%lx exited\n", this_thread);

    pthread_exit(nullptr);
}

/**
 *  向线程池的任务队列中添加任务
*/
void ThreadPool::put_task(void (*entrance)(void *arg), void *arg, int function_key)
{
    if (this->shutdown)
    {
        return;
    }

    if (unordered_addr_set->find(function_key) != unordered_addr_set->end())
    {
        return;
    }

    pthread_mutex_lock(&mutex_pool); // 涉及对任务队列的长度、队头队尾的操作

    while (task_queue->size() >= task_size)
    {
        // 任务队列满，无法添加，阻塞在这里
        pthread_cond_wait(&cond_queue_full, &mutex_pool);
    }

    Task t;
    t.entrance = entrance;
    t.arg = arg;
    t.key = function_key;
    task_queue->push(t);
    unordered_addr_set->insert(function_key);
    pthread_cond_signal(&cond_queue_empty); // 唤醒消费者线程执行任务
    pthread_mutex_unlock(&mutex_pool);
}

void ThreadPool::pool_destroy()
{
    this->shutdown = true;
    pthread_join(this->manager_thread, nullptr); // 回收管理者线程的资源

    for (int i = 0; i < this->alive_thread_num; i++)
    {
        // 唤醒因任务队列空而阻塞的线程
        pthread_cond_signal(&this->cond_queue_empty);
    }

    if (this->task_queue)
    {
        delete this->task_queue;
    }

    if (this->worker_threads)
        delete[] this->worker_threads;

    if (this->unordered_addr_set)
        delete this->unordered_addr_set;

    pthread_mutex_destroy(&this->mutex_busy_thread);
    pthread_mutex_destroy(&this->mutex_pool);
    pthread_cond_destroy(&this->cond_queue_empty);
    pthread_cond_destroy(&this->cond_queue_full);
    std::cout << "pool destroyed.\n"
              << std::endl;
}