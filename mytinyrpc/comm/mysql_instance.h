#pragma once

#include "comm/config.h"

#include <memory>
#include <string>

namespace tinyrpc {

class MySQLInstance {
 public:
    using Ptr = std::shared_ptr<MySQLInstance>;

    MySQLInstance() = default;
    ~MySQLInstance();

    MySQLInstance(const MySQLInstance&) = delete;
    MySQLInstance& operator=(const MySQLInstance&) = delete;

    bool connect(const MySQLConfig& config);
    void close();
    bool isConnected() const;
    const std::string& getLastError() const;

 private:
#ifdef MYTINYRPC_ENABLE_MYSQL
    void *m_mysql {nullptr};
#endif
    std::string m_lastError;
};

class MySQLInstanceFactory {
 public:
    static MySQLInstance& getThreadLocalInstance();
    static bool isPluginEnabled();
};

}
