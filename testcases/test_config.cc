#include "comm/config.h"
#include "comm/mysql_instance.h"
#include "net/http/httpcodec.h"
#include "net/http/httpdispatcher.h"
#include "net/netaddress.h"
#include "net/tcpserver.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdispatcher.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string writeTempConfig(const std::string& name, const std::string& content)
{
    std::filesystem::create_directories("build/config-tests");
    std::string path = "build/config-tests/" + name;
    std::ofstream output(path);
    output << content;
    return path;
}

}  // namespace

TEST(ConfigTest, DefaultsAreExplicit)
{
    tinyrpc::Config config;

    EXPECT_EQ(config.getServerHost(), "127.0.0.1");
    EXPECT_EQ(config.getServerPort(), 19999);
    EXPECT_EQ(config.getProtocol(), "tinypb");
    EXPECT_EQ(config.getIOThreadNum(), 0);
    EXPECT_EQ(config.getTimeoutMs(), 5000);
    EXPECT_EQ(config.getLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getRpcLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getAppLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getLogPath(), "logs");
    EXPECT_EQ(config.getLogPrefix(), "mytinyrpc");
    EXPECT_EQ(config.getLogMaxSizeBytes(), 64 * 1024 * 1024);
    EXPECT_EQ(config.getLogSyncIntervalMs(), 1000);
    EXPECT_EQ(config.getCoroutineStackSizeBytes(), 128 * 1024);
    EXPECT_EQ(config.getCoroutinePoolSize(), 128);
    EXPECT_FALSE(config.isCoroutinePoolExpandOnExhausted());
    EXPECT_EQ(config.getReqIdLen(), 20);
    EXPECT_EQ(config.getMaxConnectTimeoutMs(), 5000);
    EXPECT_EQ(config.getTimeWheelBucketNum(), 60);
    EXPECT_EQ(config.getTimeWheelIntervalSec(), 1);
    EXPECT_FALSE(config.isMySQLEnabled());
    EXPECT_EQ(config.getMySQLConfig().m_host, "127.0.0.1");
    EXPECT_EQ(config.getMySQLConfig().m_port, 3306);
    EXPECT_EQ(config.getMySQLConfig().m_charset, "utf8mb4");
    EXPECT_EQ(config.getMySQLConfig().m_connectTimeoutMs, 5000);
}

TEST(ConfigTest, DefaultConfigCanInitializeServer)
{
    tinyrpc::Config config;
    auto codec = std::make_shared<tinyrpc::HttpCodec>();
    auto dispatcher = std::make_shared<tinyrpc::HttpDispatcher>();

    tinyrpc::TcpServer server(
        tinyrpc::IPAddress(config.getServerHost(), 0),
        codec,
        dispatcher
    );
    server.setIOThreadNum(config.getIOThreadNum());

    EXPECT_TRUE(server.init());
    EXPECT_EQ(server.getIOThreadNum(), config.getIOThreadNum());
}

TEST(ConfigTest, LoadTinyPbXml)
{
    tinyrpc::Config config;

    ASSERT_TRUE(config.loadFromXml("conf/test_tinypb_server.xml"));
    EXPECT_EQ(config.getServerHost(), "127.0.0.1");
    EXPECT_EQ(config.getServerPort(), 24139);
    EXPECT_EQ(config.getProtocol(), "tinypb");
    EXPECT_EQ(config.getIOThreadNum(), 2);
    EXPECT_EQ(config.getTimeoutMs(), 3000);
    EXPECT_EQ(config.getLogLevel(), tinyrpc::LogLevel::Info);
    EXPECT_EQ(config.getRpcLogLevel(), tinyrpc::LogLevel::Info);
    EXPECT_EQ(config.getAppLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getLogPath(), "logs");
    EXPECT_EQ(config.getLogPrefix(), "tinypb_server");
    EXPECT_EQ(config.getLogMaxSizeBytes(), 64 * 1024 * 1024);
    EXPECT_EQ(config.getLogSyncIntervalMs(), 1000);
    EXPECT_EQ(config.getCoroutineStackSizeBytes(), 128 * 1024);
    EXPECT_EQ(config.getCoroutinePoolSize(), 128);
    EXPECT_FALSE(config.isCoroutinePoolExpandOnExhausted());
    EXPECT_EQ(config.getReqIdLen(), 20);
    EXPECT_EQ(config.getMaxConnectTimeoutMs(), 5000);
    EXPECT_EQ(config.getTimeWheelBucketNum(), 60);
    EXPECT_EQ(config.getTimeWheelIntervalSec(), 1);

    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();
    tinyrpc::TcpServer server(
        tinyrpc::IPAddress(config.getServerHost(), 0),
        codec,
        dispatcher
    );
    server.setIOThreadNum(config.getIOThreadNum());

    EXPECT_TRUE(server.init());
    EXPECT_EQ(server.getIOThreadNum(), 2);
}

TEST(ConfigTest, LoadHttpXml)
{
    tinyrpc::Config config;

    ASSERT_TRUE(config.loadFromXml("conf/test_http_server.xml"));
    EXPECT_EQ(config.getServerHost(), "127.0.0.1");
    EXPECT_EQ(config.getServerPort(), 24142);
    EXPECT_EQ(config.getProtocol(), "http");
    EXPECT_EQ(config.getIOThreadNum(), 1);
    EXPECT_EQ(config.getTimeoutMs(), 2000);
    EXPECT_EQ(config.getLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getRpcLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getAppLogLevel(), tinyrpc::LogLevel::Error);

    auto codec = std::make_shared<tinyrpc::HttpCodec>();
    auto dispatcher = std::make_shared<tinyrpc::HttpDispatcher>();
    tinyrpc::TcpServer server(
        tinyrpc::IPAddress(config.getServerHost(), 0),
        codec,
        dispatcher
    );
    server.setIOThreadNum(config.getIOThreadNum());

    EXPECT_TRUE(server.init());
    EXPECT_EQ(server.getIOThreadNum(), 1);
}

TEST(ConfigTest, MissingFieldsKeepDefaults)
{
    tinyrpc::Config config;

    ASSERT_TRUE(config.loadFromXml("conf/test_partial_server.xml"));
    EXPECT_EQ(config.getServerHost(), "0.0.0.0");
    EXPECT_EQ(config.getServerPort(), 19999);
    EXPECT_EQ(config.getProtocol(), "tinypb");
    EXPECT_EQ(config.getIOThreadNum(), 0);
    EXPECT_EQ(config.getTimeoutMs(), 5000);
    EXPECT_EQ(config.getLogLevel(), tinyrpc::LogLevel::Debug);
}

TEST(ConfigTest, ExtendedFieldsUseDefaultsWhenMissing)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task86_missing_extended.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25001</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "</config>"
    );

    ASSERT_TRUE(config.loadFromXml(path));
    EXPECT_EQ(config.getServerPort(), 25001);
    EXPECT_EQ(config.getLogPath(), "logs");
    EXPECT_EQ(config.getLogPrefix(), "mytinyrpc");
    EXPECT_EQ(config.getLogMaxSizeBytes(), 64 * 1024 * 1024);
    EXPECT_EQ(config.getRpcLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getAppLogLevel(), tinyrpc::LogLevel::Debug);
    EXPECT_EQ(config.getCoroutineStackSizeBytes(), 128 * 1024);
    EXPECT_EQ(config.getCoroutinePoolSize(), 128);
    EXPECT_FALSE(config.isCoroutinePoolExpandOnExhausted());
    EXPECT_EQ(config.getReqIdLen(), 20);
    EXPECT_EQ(config.getMaxConnectTimeoutMs(), 5000);
    EXPECT_EQ(config.getTimeWheelBucketNum(), 60);
    EXPECT_EQ(config.getTimeWheelIntervalSec(), 1);
}

TEST(ConfigTest, ParseMySQLSection)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task117_mysql.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25007</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <mysql>"
        "        <enable>true</enable>"
        "        <host>10.0.0.8</host>"
        "        <port>3307</port>"
        "        <user>rpc_user</user>"
        "        <password>rpc_pass</password>"
        "        <database>rpc_db</database>"
        "        <charset>utf8</charset>"
        "        <connect_timeout_ms>1200</connect_timeout_ms>"
        "    </mysql>"
        "</config>"
    );

    ASSERT_TRUE(config.loadFromXml(path));
    const auto& mysql = config.getMySQLConfig();
    EXPECT_TRUE(config.isMySQLEnabled());
    EXPECT_EQ(mysql.m_host, "10.0.0.8");
    EXPECT_EQ(mysql.m_port, 3307);
    EXPECT_EQ(mysql.m_user, "rpc_user");
    EXPECT_EQ(mysql.m_password, "rpc_pass");
    EXPECT_EQ(mysql.m_database, "rpc_db");
    EXPECT_EQ(mysql.m_charset, "utf8");
    EXPECT_EQ(mysql.m_connectTimeoutMs, 1200);
}

TEST(ConfigTest, InvalidMySQLSectionReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task117_invalid_mysql.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25008</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <mysql>"
        "        <enable>maybe</enable>"
        "    </mysql>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_NE(config.getLastError().find("mysql.enable"), std::string::npos);
}

TEST(ConfigTest, MySQLPluginDisabledReturnsClearNoOpError)
{
    tinyrpc::MySQLConfig mysql;
    mysql.m_enabled = true;

    tinyrpc::MySQLInstance& instance = tinyrpc::MySQLInstanceFactory::getThreadLocalInstance();
    instance.close();

    if (!tinyrpc::MySQLInstanceFactory::isPluginEnabled()) {
        EXPECT_FALSE(instance.connect(mysql));
        EXPECT_FALSE(instance.isConnected());
        EXPECT_NE(instance.getLastError().find("MYTINYRPC_ENABLE_MYSQL"), std::string::npos);
    }
}

TEST(ConfigTest, InvalidExtendedIntegerReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task86_invalid_extended.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25002</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <coroutine>"
        "        <pool_size>abc</pool_size>"
        "    </coroutine>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_FALSE(config.getLastError().empty());
}

TEST(ConfigTest, ParseCoroutinePoolExpandStrategy)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task93_coroutine_expand.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25005</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <coroutine>"
        "        <pool_size>2</pool_size>"
        "        <stack_size_kb>64</stack_size_kb>"
        "        <expand_on_exhausted>true</expand_on_exhausted>"
        "    </coroutine>"
        "</config>"
    );

    ASSERT_TRUE(config.loadFromXml(path));
    EXPECT_EQ(config.getCoroutinePoolSize(), 2);
    EXPECT_EQ(config.getCoroutineStackSizeBytes(), 64 * 1024);
    EXPECT_TRUE(config.isCoroutinePoolExpandOnExhausted());
}

TEST(ConfigTest, InvalidCoroutinePoolExpandStrategyReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task93_invalid_coroutine_expand.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25006</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <coroutine>"
        "        <expand_on_exhausted>maybe</expand_on_exhausted>"
        "    </coroutine>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_NE(config.getLastError().find("coroutine.expand_on_exhausted"), std::string::npos);
}

TEST(ConfigTest, ParseRpcAndAppLogLevels)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task86_log_levels.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25003</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <log>"
        "        <rpc_level>warn</rpc_level>"
        "        <app_level>ERROR</app_level>"
        "    </log>"
        "</config>"
    );

    ASSERT_TRUE(config.loadFromXml(path));
    EXPECT_EQ(config.getRpcLogLevel(), tinyrpc::LogLevel::Warn);
    EXPECT_EQ(config.getLogLevel(), tinyrpc::LogLevel::Warn);
    EXPECT_EQ(config.getAppLogLevel(), tinyrpc::LogLevel::Error);
}

TEST(ConfigTest, InvalidPathReturnsFalse)
{
    tinyrpc::Config config;

    EXPECT_FALSE(config.loadFromXml("conf/not_exists.xml"));
    EXPECT_FALSE(config.getLastError().empty());
}

TEST(ConfigTest, InvalidFieldTypeReturnsFalse)
{
    tinyrpc::Config config;

    EXPECT_FALSE(config.loadFromXml("conf/test_invalid_server.xml"));
    EXPECT_FALSE(config.getLastError().empty());
}

TEST(ConfigTest, InvalidProtocolReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task87_invalid_protocol.xml",
        "<config>"
        "    <server>"
        "        <protocol>grpc</protocol>"
        "    </server>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_NE(config.getLastError().find("server.protocol"), std::string::npos);
}

TEST(ConfigTest, InvalidLogLevelReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task87_invalid_log_level.xml",
        "<config>"
        "    <server>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <log>"
        "        <rpc_level>trace</rpc_level>"
        "    </log>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_NE(config.getLastError().find("log.rpc_level"), std::string::npos);
}

TEST(ConfigTest, OldFlatXmlReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task87_old_flat.xml",
        "<config>"
        "    <server_addr>127.0.0.1:25004</server_addr>"
        "    <protocol>tinypb</protocol>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_NE(config.getLastError().find("server"), std::string::npos);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[config] PASS" << std::endl;
    }
    return result;
}
