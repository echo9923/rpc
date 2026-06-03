#include "comm/reqid.h"

#include <atomic>
#include <chrono>
#include <string>

namespace tinyrpc {

std::string ReqIdUtil::genReqId()
{
    static std::atomic<unsigned long long> sequence {0};

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    unsigned long long current = sequence.fetch_add(1) + 1;

    return "tinypb-" + std::to_string(nowUs) + "-" + std::to_string(current);
}

}
