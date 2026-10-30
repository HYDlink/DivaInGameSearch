#pragma once
#include "core.h"
#include <optional>
namespace diva {
// 参数 path/name/reason 为归档、精确条目名与中文失败原因；只读普通 FArc/FArC。
std::optional<std::string> ReadFarcEntry(const fs::path& path,const std::string& name,std::string& reason);
// 参数 data/name/reason 为已读取归档字节、精确条目名与失败原因；用于CPK内存条目。
std::optional<std::string> ReadFarcEntryMemory(const std::string& data,const std::string& name,std::string& reason);
}
