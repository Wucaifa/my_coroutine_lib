#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "./1_thread/thread.h"
#include "./2_fiber/fiber.h"

#include <mutex>
#include <vector>

namespace my_coroutine_lib {

class Scheduler{

public:
    Scheduler(size_t threads = 1, bool use_scheduler = true, const std::string& name = "Scheduler");
    virtual ~Scheduler();

    const std::string& getName() const { return m_name; }

public:
    // 获取正在运行的调度器
    static Scheduler* GetThis();

protected:
    // 设置正在运行的调度器
    void SetThis();

public:
    // 添加任务到任务队列
    template<class FiberOrcb>
    void scheduleLock(FiberOrcb fc, int thread = -1){
        bool need_tickle;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            need_tickle = m_tasks.empty();

            ScheduleTask task(fc, thread);
            if(task.fiber || task.cb) {
                m_tasks.push_back(std::move(task)); // 将任务添加到任务队列
            }
        }

        if(need_tickle) {
            tickle(); // 唤醒调度器
        }
    }

    // 启动线程池
    virtual void start();

    // 停止线程池
    virtual void stop();

protected:
    virtual void tickle();

    virtual void run();     // 线程池运行函数

    virtual void idle();    // 空闲时执行的函数

    virtual bool stopping();  // 是否正在停止

    bool hasIdleThreads() { return m_idleThreadCount > 0; }

private:
    struct ScheduleTask {
        std::shared_ptr<Fiber> fiber; // 协程任务
        std::function<void()> cb;     // 普通任务
        int thread;               // 线程ID

        ScheduleTask() : fiber(nullptr), cb(nullptr), thread(-1) {}
        ScheduleTask(std::shared_ptr<Fiber> f, int t) : fiber(std::move(f)), thread(t) {}
        ScheduleTask(std::function<void()> c, int t) : cb(std::move(c)), thread(t) {}
        void reset(){
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };

private:
    std::string m_name;                // 调度器名称
    std::mutex m_mutex;              // 互斥锁，保护任务队列
    std::vector<std::shared_ptr<Thread>> m_threads; // 线程池
    std::vector<ScheduleTask> m_tasks; // 任务队列
    std::vector<int> m_threadIds; // 线程ID列表
    size_t m_threadCount = 0;         // 线程数量
    std::atomic<size_t> m_activeThreadCount = 0; // 活动线程数量
    std::atomic<size_t> m_idleThreadCount = 0;   // 空闲线程数量
    
    bool m_useCaller;   // 主线程是否使用工作线程
    std::shared_ptr<Fiber> m_schedulerFiber; // 如果是 -> 需要额外创建调度协程
    int m_rootThreadId = -1; // 如果是 -> 记录主线程的线程id
    bool m_stopping = false; // 是否正在停止调度器
};


}

#endif // _SCHEDULER_H_