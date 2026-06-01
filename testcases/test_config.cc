#include "comm/config.h"
#include "net/http/httpcodec.h"
#include "net/http/httpdispatcher.h"
#include "net/netaddress.h"
#include "net/tcpserver.h"

#include <gtest/gtest.h>

#include <iostream>
#include <memory>

TEST(ConfigTest, DefaultsAreExplicit)
{
    tinyrpc::Config config;

    EXPECT_EQ(config.getServerHost(), "127.0.0.1");
    EXPECT_EQ(config.getServerPort(), 19999);
    EXPECT_EQ(config.getProtocol(), "tinypb");
    EXPECT_EQ(config.getIOThreadNum(), 0);
    EXPECT_EQ(config.getTimeoutMs(), 5000);
    EXPECT_EQ(config.getLogLevel(), tinyrpc::LogLevel::Debug);
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

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[config] PASS" << std::endl;
    }
    return result;
}
