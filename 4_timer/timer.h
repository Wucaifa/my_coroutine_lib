#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <vector>
#include <set>
#include <shared_mutex>
#include <assert.h>
#include <functional>
#include <mutex>

namespace my_coroutine_lib {
class TimerManager;

class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;
public:
    // 从时间堆中删除定时器
    bool cancel();

    // 刷新timer
    bool refresh();

    // 重设timer的超时时间,ms:新的超时时间,from_now:是否从当前时间开始计算
    bool reset(uint64_t ms, bool from_now);

private:
    Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

private:
    // 是否循环
    bool m_recurring = false;

    // 超时时间
    uint64_t m_ms = 0;

    // 绝对超时时间
    std::chrono::time_point<std::chrono::system_clock> m_next;

    // 超时时触发的回调函数
    std::function<void()> m_cb;

    // 定时器管理器
    TimerManager* m_manager = nullptr;

private:
    // 比较函数
    struct Comparator {
        bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const {
            assert(lhs && rhs); // 确保定时器不为空
            return lhs->m_next < rhs->m_next; // 按照绝对超时时间排序
        }
    };
};

class TimerManager {
friend class Timer;
public:
    TimerManager();
    virtual ~TimerManager();

    // 添加定时器
    std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

    // 添加条件定时器
    std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

    // 获取堆中最近的超时时间
    uint64_t getNextTimeout();

    // 取出所有超时定时器的回调函数
    void listExpiredCb(std::vector<std::function<void()>>& cbs);

    // 堆中是否有定时器
    bool hasTimer();

protected:
    // 当一个最早的定时器加入到堆中->调用该函数
    virtual void onTimerInsertedAtFront() {}

    // 添加定时器到堆中
    void addTimer(std::shared_ptr<Timer> timer);

private:
    // 当系统时间改变时->调用该函数
    bool detectClockChange();

private:
    std::shared_mutex m_mutex; // 互斥锁，保护定时器堆
    std::set<std::shared_ptr<Timer>, Timer::Comparator> m_timers;   // 为什么不使用 std::priority_queue？因为std::priority_queue不支持迭代器，无法遍历所有定时器
    bool m_tickled = false; // 是否有定时器被唤醒
    std::chrono::time_point<std::chrono::system_clock> m_lastTime; // 上次检测的系统时间
};

}

#endif // __SYLAR_TIMER_H__
