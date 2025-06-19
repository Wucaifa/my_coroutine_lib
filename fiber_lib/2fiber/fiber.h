#ifndef _COROUTINE_H
#define _COROUTINE_H

#include <iostream>
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>
#include <ucontext.h>
#include <unistd.h>
#include <mutex>

namespace my_coroutine_lib {

class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    // 定义协程的状态
    enum State {
        READY,      // 准备就绪
        RUNNING,    // 正在运行
        TERM        // 结束
    };

private:
    Fiber(); // 默认构造函数私有，只能被GetThis调用，用于创建主协程

public:
    Fiber(std::function<void()> cb, size_t stack_size = 0, bool run_in_scheduler = true);
    ~Fiber();

    // 重用协程
    void reset(std::function<void()> cb);

    // 任务线程恢复执行
    void resume();
    // 任务线程让出执行权
    void yield();

    // 获取当前协程ID
    uint64_t getId() const { return m_id; }
    // 获取当前协程状态
    State getState() const { return m_state; }

public:
    // 设置当前协程
    static void SetThis(Fiber* fiber);

    // 获取当前协程
    static std::shared_ptr<Fiber> GetThis();

    // 设置协程调度（默认为主协程）
    static void SetSchedulerFiber(Fiber* f);

    // 获取当前协程id
    static uint64_t GetFiberId();

    // 协程函数
    static void MainFunc();

private:
    uint64_t m_id = 0;                  // 协程ID
    uint32_t m_stacksize = 0;           // 协程栈大小
    State m_state = READY;              // 协程状态
    ucontext_t m_ctx;                   // 协程上下文
    void* m_stack = nullptr;            // 协程栈指针
    std::function<void()> m_cb;         // 协程函数
    bool m_runInScheduler;              // 是否在调度器协程中运行

public:
    std::mutex m_mutex; // 协程锁，防止多线程访问冲突
};    

}

#endif // _COROUTINE_H