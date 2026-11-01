#pragma once
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace diva {
inline constexpr auto kSearchDebounce=std::chrono::milliseconds(300);
// 参数debounce/now分别为是否防抖和可注入的单调时刻，两类搜索共用同一截止时间策略。
inline auto SearchDeadline(bool debounce,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now()) {
    return now+(debounce?kSearchDebounce:std::chrono::milliseconds(0));
}
// 参数revision/applied/due/composing为查询修订、发布修订、截止时间及组词状态。
inline bool SearchDue(uint64_t revision,uint64_t applied,std::chrono::steady_clock::time_point due,bool composing,
                      std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now()) {
    return revision!=applied && !composing && now>=due;
}
// 参数future/accept/fail为非阻塞查询任务及结果处理，业务仅负责各自结果映射和修订校验。
template<class Reply,class Accept,class Fail>
void ConsumeSearchReply(std::future<Reply>& future,Accept accept,Fail fail) {
    if(!future.valid() || future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    try{accept(future.get());}catch(...){fail();}
}
// 歌曲与服装共用一个长驻查询线程；目录读取和图片解码不占用该线程。
class SearchExecutor {
    std::mutex mutex;
    std::condition_variable wake;
    std::queue<std::function<void()>> jobs;
    std::thread worker;
    bool stopping=false;
    // 等待队列任务，在锁外执行；任务只捕获不可变快照，不接触UI对象。
    void Run() {
        for(;;){
            std::function<void()> job;
            {std::unique_lock lock(mutex);wake.wait(lock,[this]{return stopping || !jobs.empty();});
             if(stopping && jobs.empty())return;job=std::move(jobs.front());jobs.pop();}
            job();
        }
    }
public:
    // 延迟到首条查询创建线程，空视图不产生后台线程。
    SearchExecutor()=default;
    // 离线宿主销毁时收完已有任务；生产Host不在帧回调或DllMain中销毁。
    ~SearchExecutor(){ {std::lock_guard lock(mutex);stopping=true;}wake.notify_all();if(worker.joinable())worker.join();}
    // 参数work为捕获快照的纯查询，返回保留原有版本控制协议的future。
    template<class Work> auto Submit(Work work) {
        using Reply=std::invoke_result_t<Work>;
        auto task=std::make_shared<std::packaged_task<Reply()>>(std::move(work));auto result=task->get_future();
        {std::lock_guard lock(mutex);if(!worker.joinable())worker=std::thread([this]{Run();});jobs.emplace([task]{(*task)();});}
        wake.notify_one();return result;
    }
};
}
