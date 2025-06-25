#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "3_scheduler/scheduler.h"
#include "4_timer/timer.h"

namespace my_coroutine_lib {

// 1 注册事件 -> 2 等待事件 -> 3 事件触发调度回调 -> 4 注销事件回调后从epoll注销 -> 5 执行回调进入调度器中执行调度
class IOManager : public Scheduler, public TimerManager {
public:
    enum Event{
        NONE = 0x0,
        READ = 0x1,     // EPOLLIN
        WRITE = 0x4     // EPOLLOUT
    };

private:
    struct FdContext {
        struct EventContext {
            Scheduler *scheduler = nullptr; // 事件调度器
            std::shared_ptr<Fiber> fiber; // 事件对应的协程
            std::function<void()> cb; // 事件对应的回调函数
        };
        EventContext read;  // 读事件上下文
        EventContext write; // 写事件上下文
        int fd = 0; // 文件描述符
        Event events = NONE; // 当前事件状态
        std::mutex mutex; // 互斥锁，保护事件上下文

        EventContext& getEventContext(Event event);
        void resetEventContext(EventContext& ctx);
        void triggerEvent(Event event);
    };

public:
    IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "IOManager");

    ~IOManager();

    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);
    bool cancelAll(int fd);

    static IOManager* GetThis() {
        return dynamic_cast<IOManager*>(Scheduler::GetThis());
    }

protected:
    void tickle() override;
    void idle() override;
    bool stopping() override;

    void onTimerInsertedAtFront() override;
    void contextResize(size_t size);

private:
    int m_epfd = 0;
    int m_tickleFds[2]; // fd[0] read，fd[1] write
    std::atomic<size_t> m_pendingEventCount = 0; // 待处理事件数量
    std::shared_mutex m_mutex;
    std::vector<FdContext*> m_fdContexts;
};

}

#endif // __SYLAR_IOMANAGER_H__