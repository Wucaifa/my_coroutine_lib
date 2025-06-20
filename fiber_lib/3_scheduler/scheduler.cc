#include "scheduler.h"

static bool debug = false; // 是否开启调试模式

namespace my_coroutine_lib {

static thread_local Scheduler* t_scheduler = nullptr; // 当前线程的调度器

Scheduler* Scheduler::GetThis() {
    return t_scheduler; // 返回当前线程的调度器
}

void Scheduler::SetThis() {
    t_scheduler = this; // 设置当前线程的调度器
}

Scheduler::Scheduler(size_t threads, bool use_scheduler, const std::string& name)
    : m_name(name), m_useCaller(use_scheduler) {
        
    assert(threads > 0 && Scheduler::GetThis() == nullptr); // 确保线程数大于0且当前没有调度器

    SetThis(); // 设置当前线程的调度器

    Thread::SetName(m_name); // 设置线程名称

    if(use_scheduler){
        // 主线程本身也要参与调度任务,只需要再创建 N-1 个子线程就够了
        threads--;
        Fiber::GetThis(); // 创建主协程
        m_schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false)); // 创建调度器协程,false表示调度协程退出后将会返回主协程
        Fiber::SetSchedulerFiber(m_schedulerFiber.get()); // 设置调度器协程

        m_rootThreadId = Thread::GetThreadId(); // 记录主线程的线程ID
        m_threadIds.push_back(m_rootThreadId); // 将主线程ID添加到线程ID列表
    }

    m_threadCount = threads; // 设置线程数量
    if(debug) {
        std::cout << "Scheduler::Scheduler() success\n";
    }
}

Scheduler::~Scheduler() {
    assert(stopping() == true); // 确保调度器正在停止
    if(GetThis() == this){
        t_scheduler = nullptr; // 清除当前线程的调度器
    }
    if(debug) {
        std::cout << "Scheduler::~Scheduler() success\n";
    }
}

void Scheduler::start() {
    std::lock_guard<std::mutex> lock(m_mutex); // 锁定互斥锁，防止多线程访问冲突
    if(m_stopping) {
        std::cerr << "Scheduler::start() error: scheduler is stopping\n";
        return; // 如果调度器已经停止，则直接返回
    }

    assert(m_threads.empty()); // 确保线程池为空
    m_threads.resize(m_threadCount); // 根据线程数量调整线程池大小
    for(size_t i = 0; i < m_threadCount; ++i) {
        m_threads[i].reset(new Thread(
            std::bind(&Scheduler::run, this), 
            m_name + "_" + std::to_string(i))); // 创建线程并绑定调度器运行函数
        m_threadIds.push_back(m_threads[i]->getId()); // 将线程ID添加到线程ID列表
    }
    if(debug) {
        std::cout << "Scheduler::start() success, thread count: " << m_threadCount << "\n";
    }
}

void Scheduler::run(){
    int thread_id = Thread::GetThreadId(); // 获取当前线程ID
    if(debug) {
        std::cout << "Scheduler::run() thread id: " << thread_id << "\n";
    }
    SetThis(); // 设置当前线程的调度器
    if(thread_id != m_rootThreadId){
        Fiber::GetThis(); // 分配了线程的主协程和调度协程
    } 

    std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this)); // 创建空闲协程
    ScheduleTask task; // 创建任务对象

    while(true){
        task.reset(); // 重置任务对象
        bool tickle_me = false; // 是否需要唤醒其它线程进行任务调度

        {
            std::lock_guard<std::mutex> lock(m_mutex); // 锁定互斥锁，防止多线程访问冲突
            auto it = m_tasks.begin(); // 获取任务队列的迭代器
            // 1 遍历任务队列，查找匹配的任务
            while(it != m_tasks.end()) {
                if(it->thread != -1 && it->thread != thread_id) {
                    ++it; // 如果任务线程ID不匹配，则跳过该任务
                    tickle_me = true; // 标记需要唤醒其它线程
                    continue;
                }

                // 2 取出任务
                assert(it->fiber || it->cb); // 确保任务对象不为空
                task = std::move(*it); // 移动任务对象
                it = m_tasks.erase(it); // 从任务队列中移除该任务
                m_activeThreadCount++; // 活动线程数量加1
                break;
            }
            if(it != m_tasks.end()){
                tickle_me = true; // 如果找到任务，则需要唤醒其它线程
            }
        }
        
        if(tickle_me) {
            tickle(); // 唤醒其它线程进行任务调度
        }

        // 3 执行任务
        if(task.fiber) {
            {
                std::lock_guard<std::mutex> lock(task.fiber->m_mutex);
                if(task.fiber->getState() != Fiber::TERM){
                    task.fiber->resume(); // 恢复协程执行
                }
            }
            m_activeThreadCount--; // 活动线程数量减1
            task.reset(); // 重置任务对象
        }else if(task.cb) {
            std::shared_ptr<Fiber> cb_fiber = std::make_shared<Fiber>(task.cb); // 创建普通任务协程
            {
                std::lock_guard<std::mutex> lock(cb_fiber->m_mutex);
                cb_fiber->resume(); // 恢复普通任务协程执行
            }
            m_activeThreadCount--; // 活动线程数量减1
            task.reset(); // 重置任务对象
        }else{              // 4 执行空闲协程
            // 系统关闭 -> idle协程将从死循环跳出并结束 -> 此时的idle协程状态为TERM -> 再次进入将跳出循环并退出run()
            if(idle_fiber->getState() == Fiber::TERM) {
                {
                    if(debug) {
                        std::cout << "Schedule::run() ends in thread: " << thread_id << std::endl;
                    }
                    break; // 如果空闲协程已经结束，则退出循环
                }
                
                m_idleThreadCount++;
                idle_fiber->resume(); // 恢复空闲协程执行
                m_idleThreadCount--; // 空闲线程数量减1
            }
        }
    }
}

void Scheduler::stop() {
    if(debug) {
        std::cout << "Schedule::stop() starts in thread: " << Thread::GetThreadId() << std::endl;
    }

    if(stopping()) {
        return;
    }
    
    m_stopping = true; // 设置调度器为停止状态
    if(m_useCaller){
        assert(GetThis() == this); // 确保当前线程的调度器是本调度器
    }else{
        assert(GetThis() != this); // 确保当前线程的调度器不是本调度器
    }

    for(size_t i = 0; i < m_threadCount; ++i) {
        tickle(); // 唤醒所有线程进行任务调度,从而发现 m_stopping == true 后自然退出。
    }

    // 如果主线程参与了调度，需要 resume 它的 scheduler fiber 以完成退出
    if (m_useCaller && m_schedulerFiber) {
        m_schedulerFiber->resume();
        if (debug) std::cout << "m_schedulerFiber finished on thread: " << Thread::GetThreadId() << std::endl;
    }

    // 清理线程
    std::vector<std::shared_ptr<Thread>> threads;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        threads.swap(m_threads);
    }

    for (auto& t : threads) {
        t->join();
    }

    if (debug) std::cout << "Scheduler::stop() ends in thread: " << Thread::GetThreadId() << std::endl;
}

void Scheduler::tickle(){

}

void Scheduler::idle(){
	while(!stopping())
	{
		if(debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;	
		sleep(1);	
		Fiber::GetThis()->yield();
	}
}

bool Scheduler::stopping() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

}