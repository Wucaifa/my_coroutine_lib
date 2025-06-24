#include "timer.h"

namespace my_coroutine_lib {

bool Timer::cancel(){
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    if(m_cb == nullptr) {
        return false; // 如果回调函数为空，表示定时器已经被取消
    }
    else{
        m_cb = nullptr;
    }

    auto it = m_manager->m_timers.find(shared_from_this());
    if(it != m_manager->m_timers.end()) {
        m_manager->m_timers.erase(it); // 从定时器堆中删除定时器
    }
    return true; // 成功取消定时器
}

// 向后重设定时器的超时时间
bool Timer::refresh(){
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    if(m_cb == nullptr) {
        return false; // 如果回调函数为空，表示定时器已经被取消
    }

    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false; // 如果定时器不在堆中，表示定时器
    }

    m_manager->m_timers.erase(it); // 从堆中删除定时器
    m_next = std::chrono::system_clock::now() + std::chrono::milliseconds(m_ms); // 刷新绝对超时时间
    m_manager->m_timers.insert(shared_from_this()); // 重新插入到堆中
    return true; // 成功刷新定时器
}

bool Timer::reset(uint64_t ms, bool from_now){
    if(ms == m_ms && !from_now) {
        return true; // 如果超时时间没有变化，直接返回
    }

    {
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

        if(m_cb == nullptr) {
            return false; // 如果回调函数为空，表示定时器已经被取消
        }

        auto it = m_manager->m_timers.find(shared_from_this());
        if(it == m_manager->m_timers.end()) {
            return false; // 如果定时器不在堆中，表示定时器
        }
        m_manager->m_timers.erase(it); // 从堆中删除定时器
    }

    auto start = from_now ? std::chrono::system_clock::now() : m_next - std::chrono::milliseconds(m_ms); // 根据from_now决定起始时间
    m_ms = ms; // 更新超时时间
    m_next = start + std::chrono::milliseconds(m_ms); // 计算新的绝对超时时间
    m_manager->addTimer(shared_from_this()); // 重新插入到堆中
    return true; // 成功重设定时器
}

TimerManager::TimerManager(){
    m_lastTime = std::chrono::system_clock::now(); // 初始化上次检测的系统时间
}

TimerManager::~TimerManager(){

}

std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) {
    if(ms == 0 || !cb) {
        return nullptr; // 如果超时时间为0或回调函数为空，返回空指针
    }

    auto timer = std::make_shared<Timer>(ms, std::move(cb), recurring, this); // 创建定时器对象
    addTimer(timer); // 添加定时器到堆中
    return timer; // 返回定时器的shared_ptr
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> cond = weak_cond.lock(); // 尝试获取条件的shared_ptr
    if(cond) {
        cb(); // 如果条件存在，执行回调函数
    }
}

std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring) {
    return addTimer(ms, std::bind(OnTimer, weak_cond, std::move(cb)), recurring); // 添加条件定时器
}

uint64_t TimerManager::getNextTimeout() {
    std::shared_lock<std::shared_mutex> read_lock(m_mutex); // 共享锁，允许多个线程读取
    
    m_tickled = false; // 重置tickled状态

    if(m_timers.empty()) {
        return ~0ull; // 如果没有定时器，返回最大值
    }

    auto now = std::chrono::system_clock::now(); // 获取当前系统时间
    auto time = (*m_timers.begin())->m_next; // 获取最早的定时器的绝对超时时间

    if(now >= time) {
        return 0; // 如果当前时间已经超过最早的定时器超时时间，返回0
    }
    else {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now); // 计算当前时间到最早定时器的剩余时间
        return static_cast<uint64_t>(duration.count()); // 返回剩余时间的毫秒数
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs) {
    auto now = std::chrono::system_clock::now(); // 获取当前系统时间

    std::unique_lock<std::shared_mutex> write_lock(m_mutex); // 独占锁，防止其他线程修改定时器堆

    bool rollover = detectClockChange(); // 检测系统时间是否出现错误

    // 回退->清理所有timer || 超时->清理所有超时定时器,如果rollover为false就没有发生系统时间回退
    while(!m_timers.empty() && (rollover || (*m_timers.begin())->m_next <= now)) {
        std::shared_ptr<Timer> timer = *m_timers.begin(); // 获取最早的定时器
        m_timers.erase(m_timers.begin()); // 从堆中删除最早的

        cbs.push_back(std::move(timer->m_cb)); // 将定时器的回调函数添加到回调函数列表中
        // 如果定时器是循环的，则重新设置其超时时间并添加到堆中
        if(timer->m_recurring) {
            timer->m_next = now + std::chrono::milliseconds(timer->m_ms); // 设置新的绝对超时时间
            m_timers.insert(timer); // 重新插入到堆中
        }
        else{
            timer->m_cb = nullptr; // 如果不是循环的，清空回调函数
        }
    }
}

bool TimerManager::hasTimer() {
    std::shared_lock<std::shared_mutex> read_lock(m_mutex); // 共享锁，允许多个线程读取
    return !m_timers.empty(); // 如果定时器堆不为空，返回true
}

void TimerManager::addTimer(std::shared_ptr<Timer> timer) {
    bool at_front = false; // 是否将定时器添加到堆的最前面

    {
        std::unique_lock<std::shared_mutex> write_lock(m_mutex); // 独占锁，防止其他线程修改定时器堆
        auto it = m_timers.insert(timer).first; // 将定时器插入到堆中，并获取迭代器
        at_front = (it == m_timers.begin()) && !m_tickled; // 如果是最早的定时器，设置at_front为true
        if(at_front) {
            m_tickled = true; // 如果是最早的定时器，设置tickled状态为true
        }
    }

    if(at_front) {
        onTimerInsertedAtFront(); // 如果是最早的定时器，调用插入到前端的回调函数
    }
}

bool TimerManager::detectClockChange() {
    bool changed = false; // 是否检测到系统时间变化
    auto now = std::chrono::system_clock::now(); // 获取当前系统时间
    if(now < m_lastTime - std::chrono::milliseconds(60 * 60 *1000)) {
        changed = true; // 如果当前时间比上次检测的时间早1小时，表示系统时间发生了回退
    }
    m_lastTime = now; // 更新上次检测的时间
    return changed;
}

}