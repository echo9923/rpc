#include "comm/mysql_instance.h"

#ifdef MYTINYRPC_ENABLE_MYSQL
#include <algorithm>

#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#elif __has_include(<mysql.h>)
#include <mysql.h>
#else
#error "MYTINYRPC_ENABLE_MYSQL requires mysql/mysql.h or mysql.h"
#endif
#endif

namespace tinyrpc {

namespace {

#ifdef MYTINYRPC_ENABLE_MYSQL
MYSQL* toMySQL(void *handle)
{
    return static_cast<MYSQL *>(handle);
}

int toTimeoutSeconds(int timeoutMs)
{
    if (timeoutMs <= 0) {
        return 1;
    }
    return std::max(1, (timeoutMs + 999) / 1000);
}
#endif

}  // namespace

MySQLInstance::~MySQLInstance()
{
    close();
}

bool MySQLInstance::connect(const MySQLConfig& config)
{
    close();
    if (!config.m_enabled) {
        m_lastError = "mysql config is disabled";
        return false;
    }

#ifndef MYTINYRPC_ENABLE_MYSQL
    m_lastError = "mysql plugin is disabled, rebuild with -DMYTINYRPC_ENABLE_MYSQL=ON";
    return false;
#else
    // [第三方 API] mysql_init 创建 MySQL C API 连接句柄；参数为 nullptr 时由库内部分配。
    MYSQL *mysql = mysql_init(nullptr);
    if (mysql == nullptr) {
        m_lastError = "mysql_init failed";
        return false;
    }

    unsigned int timeoutSec = static_cast<unsigned int>(toTimeoutSeconds(config.m_connectTimeoutMs));
    // [第三方 API] mysql_options 设置连接超时，MYSQL_OPT_CONNECT_TIMEOUT 的参数单位是秒。
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSec);

    const char *database = config.m_database.empty() ? nullptr : config.m_database.c_str();
    // [第三方 API] mysql_real_connect 发起 TCP/MySQL 握手；host/user/password/db/port
    // 分别对应目标地址、认证信息、默认库和端口。失败时 mysql_error 返回库维护的错误文本。
    if (mysql_real_connect(
            mysql,
            config.m_host.c_str(),
            config.m_user.c_str(),
            config.m_password.c_str(),
            database,
            config.m_port,
            nullptr,
            0) == nullptr) {
        m_lastError = mysql_error(mysql);
        mysql_close(mysql);
        return false;
    }

    if (!config.m_charset.empty() && mysql_set_character_set(mysql, config.m_charset.c_str()) != 0) {
        m_lastError = mysql_error(mysql);
        mysql_close(mysql);
        return false;
    }

    m_mysql = mysql;
    m_lastError.clear();
    return true;
#endif
}

void MySQLInstance::close()
{
#ifdef MYTINYRPC_ENABLE_MYSQL
    if (m_mysql != nullptr) {
        // [第三方 API] mysql_close 释放连接句柄，并关闭底层 MySQL 网络连接。
        mysql_close(toMySQL(m_mysql));
        m_mysql = nullptr;
    }
#endif
}

bool MySQLInstance::isConnected() const
{
#ifdef MYTINYRPC_ENABLE_MYSQL
    return m_mysql != nullptr;
#else
    return false;
#endif
}

const std::string& MySQLInstance::getLastError() const
{
    return m_lastError;
}

MySQLInstance& MySQLInstanceFactory::getThreadLocalInstance()
{
    thread_local MySQLInstance instance;
    return instance;
}

bool MySQLInstanceFactory::isPluginEnabled()
{
#ifdef MYTINYRPC_ENABLE_MYSQL
    return true;
#else
    return false;
#endif
}

}
