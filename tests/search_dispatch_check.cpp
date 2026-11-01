#include "search_dispatch.h"
#include "performance_trace.h"
#include <iostream>
#include <stdexcept>
// 参数condition/message为断言结果及中文原因。
static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
// 用虚拟时间验证连续输入、IME及立即确认；用真实任务验证线程复用和异常消费。
int main(){
    using namespace diva;using namespace std::chrono;
    try{
        const auto start=steady_clock::time_point{};
        auto due=SearchDeadline(true,start);
        Check(!SearchDue(2,1,due,false,start+milliseconds(299)),"防抖截止前提前启动");
        due=SearchDeadline(true,start+milliseconds(250));
        Check(!SearchDue(3,1,due,false,start+milliseconds(300)),"连续输入未推迟截止时间");
        Check(SearchDue(3,1,due,false,start+milliseconds(550)),"停止输入后未启动");
        Check(!SearchDue(3,1,due,true,start+seconds(2)),"IME组词期间查询意外启动");
        Check(SearchDue(3,1,SearchDeadline(false,start),false,start),"Enter或筛选未立即调度");
        Check(!SearchDue(3,3,due,false,start+seconds(2)),"已发布修订重复启动");
        SearchExecutor executor;
        auto a=executor.Submit([]{return std::this_thread::get_id();});auto first=a.get();
        auto b=executor.Submit([]{return std::this_thread::get_id();});Check(b.get()==first && first!=std::this_thread::get_id(),"查询没有复用后台线程");
        auto broken=executor.Submit([]()->int{throw std::runtime_error("受控测试异常");});broken.wait();bool caught=false;
        ConsumeSearchReply(broken,[](int){},[&]{caught=true;});Check(caught && !broken.valid(),"异常未消费或任务未释放");
        auto recovery=executor.Submit([]{return 42;});Check(recovery.get()==42,"异常后线程无法继续查询");
        PerformanceFrame sample;sample.gap=30;sample.total=2;sample.inputToDraw=8;
        PerformanceQuery measured{"歌曲",9,{100,400,405,407,412},420,3};
        const auto report=PerformanceTrace::Report({sample},{measured});
        Check(report.find("305.000\t5.000\t2.000\t5.000\t8.000\t3.000\t320.000")!=std::string::npos,
            "性能报告没有独立区分调度、防抖、排队、查询和发布耗时");
        Check(PerformanceTrace::Percentile({sample},&PerformanceFrame::inputToDraw)==8,"输入延迟百分位统计错误");
        std::cout<<"共用查询调度通过：300毫秒防抖、连续输入、组词、立即确认、线程复用与异常恢复。\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
