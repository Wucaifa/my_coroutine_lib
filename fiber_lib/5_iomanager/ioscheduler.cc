#include <unistd.h>    
#include <sys/epoll.h> 
#include <fcntl.h>     
#include <cstring>

#include "ioscheduler.h"

static bool debug = true;

namespace my_coroutine_lib {

IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event) {
    assert(event == READ || event == WRITE); // 确保事件类型正确
    switch(event) {
        case READ:
            return read; // 返回读事件上下文
        case WRITE:
            return write; // 返回写事件上下文
        default:
            throw std::logic_error("Invalid event type");
    }
}

void IOManager::FdContext::resetEventContext(EventContext& ctx) {
    ctx.scheduler = nullptr; // 重置事件调度器
    ctx.fiber.reset(); // 重置协程
    ctx.cb = nullptr; // 清空回调函数
}

void IOManager::FdContext::triggerEvent(Event event) {
    assert(events & event); // 确保当前事件状态包含触发的事件
    events = (Event)(events & ~event);  // 删除触发的事件

    EventContext& ctx = getEventContext(event); // 获取事件上下文
    if(ctx.cb){
        ctx.scheduler->scheduleLock(&ctx.cb);   // 执行cb
    }else{
        ctx.scheduler->scheduleLock(&ctx.fiber);    // 执行协程
    }

    resetEventContext(ctx); // 重置事件上下文
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    : Scheduler(threads, use_caller, name), TimerManager() {
    m_epfd = epoll_create(5000); // 创建epoll实例
    assert(m_epfd > 0);
    
    int rt = pipe(m_tickleFds); // 成功返回0，失败返回-1
    assert(!rt);
    
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // 设置边缘触发模式
    ev.data.fd = m_tickleFds[0]; // 监听读端

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK); // 设置非阻塞模式
    assert(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &ev); // 将读端添加到epoll中
    assert(!rt);

    contextResize(32);

    start();
}

IOManager::~IOManager(){
    stop();
    close(m_epfd); // 关闭epoll实例
    close(m_tickleFds[0]); // 关闭读端
    close(m_tickleFds[1]); // 关闭写端

    for(auto& fd_ctx : m_fdContexts) {
        if(fd_ctx) {
            delete fd_ctx; // 释放FdContext对象
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size); // 调整FdContext数组大小
    for(size_t i = 0; i < size; ++i) {
        if(m_fdContexts[i] == nullptr){
            m_fdContexts[i] = new FdContext();
            m_fdContexts[i]->fd = i;
        }
    }
}

}