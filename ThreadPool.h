#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(size_t);

    // 模板，匹配可变参数
    // std::result_of：编译期推导出模板返回值类型
    // std::future：可以异步存储线程中task返回值
    template<class F, class... Args>
    auto enqueue(F &&f, Args &&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>;

    ~ThreadPool();

private:
    // 线程对象
    std::vector<std::thread> workers;
    // 用于接收void类型的lambda，其中循环空转
    std::queue<std::function<void()> > tasks;

    // 相关同步和异步
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
ThreadPool::ThreadPool(size_t threads)
        : stop(false) {
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back(  // 根据线程池预设大小，每个线程创建一个void类型的lambda，每个lambda中跑的逻辑：循环从任务队列中取出任务（任务也是void类型的lambda）执行
                [this] {  // 捕捉this指针
                    for (;;) {
                        std::function<void()> task; // 用于存储从任务队列中取出的lambda

                        {// 对任务队列的同步互斥操作
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                                 [this] { return this->stop || !this->tasks.empty(); });
                            if (this->stop && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }
                        // 执行从任务队列中取出的lambda
                        task();
                    }
                }
        );
}

// 添加新的任务到任务队列里，任务即是要被执行的函数以及对应的参数
// 任务并不是直接被任务队列接收，任务队列只能接收void()类型的函数，所以每个任务要用void()类型的lambda封装
template<class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&... args)
-> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    // std::packaged_task和std::bind结合可以将任意类型的函数封装成一个可调用对象task
    // 同时这个可调用对象的返回值能用std::future来存储，也就是先用task.get_future得到一个std::future对象
    // std::future::get行为会阻塞等待可调用对象task的返回结果
    auto task = std::make_shared<std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {// 同步互斥操作
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        // 每个task的执行用lambda进行封装，便于存放在任务队列里
        tasks.emplace([task]() { (*task)(); });
    }
    // 主线程对任务队列操作完成，唤醒其他子线程
    condition.notify_one();
    // 返回std::future对象，用于外部获取task的返回值
    return res;
}

// 销毁对象
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker: workers)
        worker.join();
}

#endif
