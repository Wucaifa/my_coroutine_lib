#ifndef _THREAD_H
#define _THREAD_H

#include <mutex>
#include <condition_variable>
#include <functional>

namespace my_coroutine_lib 
{

// 用于线程方法间的同步
class Semaphore {
private:
    std::mutex mtx;
    std::condition_variable cv;
    int count;

public:
    // 信号量初始化为0
    explicit Semaphore(int initial_count = 0) : count(initial_count) {}

    // P操作：等待信号量
    void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        while(count == 0){
            cv.wait(lock);
        }
        --count;
    }

    // V操作：释放信号量
    void signal() {
        std::unique_lock<std::mutex> lock(mtx);
        ++count;
        cv.notify_one();
    }
};

// 两种线程：1.由系统自动创建的主线程  2.由Thread类创建的线程
class Thread {
public:
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();

    pid_t getId() const { return m_id; };
    const std::string& getName() const { return m_name; };

    // 启动线程
    void join();

public:
    static pid_t GetThreadId();     // 获取当前线程ID

    static Thread* GetThis();       // 获取当前线程对象

    static const std::string& GetName();    // 获取当前线程名称

    static void SetName(const std::string& name);   // 设置当前线程名称

private:
    // 线程执行的函数
    static void* run(void* arg);

private:
    pid_t m_id = -1; // 线程ID，-1表示未创建
    pthread_t m_thread = 0; // 线程对象

    std::function<void()> m_cb; // 线程执行的回调函数
    std::string m_name; // 线程名称

    Semaphore m_semaphore; // 信号量，用于线程间同步
};
}

#endif // _THREAD_H