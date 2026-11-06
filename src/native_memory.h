#pragma once
#include "native_layout.h"
#include <Windows.h>
#include <cstdint>

// 访问当前进程内游戏内存的唯一入口：不可访问时返回 false，不改变页权限，也不触发访问异常。
namespace diva::native {

// 参数 address/value 为当前进程地址与接收值；读取失败时 value 的内容不可信。
template<class T> bool ReadAt(uintptr_t address,T& value) {
    SIZE_T count=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}

// 参数 address/value 为目标地址与写入值；只写已提交且可写的页。
template<class T> bool WriteAt(uintptr_t address,T value) {
    MEMORY_BASIC_INFORMATION info{};SIZE_T count=0;
    return address && VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
        && WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}

// 参数 address 为候选函数入口；只接受已提交的可执行页，入口被其他 Mod 改成跳转也不影响判断。
inline bool IsExecutable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}

// 参数 nt 为主程序 PE 头；核对是否为 build 记录的已核查构建。SHA-256 由模型定位器另行核对。
inline bool IsVerifiedBuild(const IMAGE_NT_HEADERS64& nt) {
    return nt.FileHeader.TimeDateStamp==build::kTimestamp && nt.OptionalHeader.SizeOfImage==build::kImageSize;
}

}
