#include "comm/config.h"
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
    EXPECT_EQ(config.getReqIdLen(), 20);
    EXPECT_EQ(config.getMaxConnectTimeoutMs(), 5000);
    EXPECT_EQ(config.getTimeWheelBucketNum(), 60);
    EXPECT_EQ(config.getTimeWheelIntervalSec(), 1);
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
        "    <server_addr>127.0.0.1:25001</server_addr>"
        "    <protocol>tinypb</protocol>"
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
    EXPECT_EQ(config.getReqIdLen(), 20);
    EXPECT_EQ(config.getMaxConnectTimeoutMs(), 5000);
    EXPECT_EQ(config.getTimeWheelBucketNum(), 60);
    EXPECT_EQ(config.getTimeWheelIntervalSec(), 1);
}

TEST(ConfigTest, InvalidExtendedIntegerReturnsFalse)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task86_invalid_extended.xml",
        "<config>"
        "    <server_addr>127.0.0.1:25002</server_addr>"
        "    <protocol>tinypb</protocol>"
        "    <cor_pool_size>abc</cor_pool_size>"
        "</config>"
    );

    EXPECT_FALSE(config.loadFromXml(path));
    EXPECT_FALSE(config.getLastError().empty());
}

TEST(ConfigTest, ParseRpcAndAppLogLevels)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task86_log_levels.xml",
        "<config>"
        "    <server_addr>127.0.0.1:25003</server_addr>"
        "    <protocol>tinypb</protocol>"
        "    <log_level>warn</log_level>"
        "    <app_log_level>ERROR</app_log_level>"
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

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[config] PASS" << std::endl;
    }
    return result;
}
