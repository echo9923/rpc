#include "comm/log.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string readFile(const std::string& path)
{
    std::ifstream input(path);
    std::string content(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    return content;
}

std::string makeLogPath(const std::string& name)
{
    std::filesystem::create_directories("build/log-tests");
    std::string path = "build/log-tests/" + name;
    std::filesystem::remove(path);
    return path;
}

void resetLogger(const std::string& path)
{
    tinyrpc::Logger::shutdown();
    std::filesystem::remove(path);
}

}  // namespace

TEST(LoggerTest, LevelFilteringWritesAtOrAboveLevel)
{
    std::string path = makeLogPath("level-filter.log");
    ASSERT_TRUE(tinyrpc::Logger::init(path, tinyrpc::LogLevel::Info));

    DebugLog("debug skipped");
    InfoLog("info kept");
    WarnLog("warn kept");
    tinyrpc::Logger::flush();

    std::string content = readFile(path);
    EXPECT_EQ(content.find("debug skipped"), std::string::npos);
    EXPECT_NE(content.find("info kept"), std::string::npos);
    EXPECT_NE(content.find("warn kept"), std::string::npos);

    resetLogger(path);
}

TEST(LoggerTest, FileOutputContainsThreadFileLineAndReqId)
{
    std::string path = makeLogPath("format.log");
    ASSERT_TRUE(tinyrpc::Logger::init(path, tinyrpc::LogLevel::Debug));

    tinyrpc::Logger::log(
        tinyrpc::LogLevel::Error,
        "unit_file.cc",
        77,
        "method=QueryService.query_name err=100",
        "req-001"
    );
    tinyrpc::Logger::flush();

    std::string content = readFile(path);
    EXPECT_NE(content.find("[ERROR]"), std::string::npos);
    EXPECT_NE(content.find("[tid="), std::string::npos);
    EXPECT_NE(content.find("[unit_file.cc:77]"), std::string::npos);
    EXPECT_NE(content.find("[reqId=req-001]"), std::string::npos);
    EXPECT_NE(content.find("method=QueryService.query_name"), std::string::npos);
    EXPECT_NE(content.find("err=100"), std::string::npos);

    resetLogger(path);
}

TEST(LoggerTest, FlushMakesFileReadable)
{
    std::string path = makeLogPath("flush.log");
    ASSERT_TRUE(tinyrpc::Logger::init(path, tinyrpc::LogLevel::Debug));

    InfoLog("flush visible");
    tinyrpc::Logger::flush();

    EXPECT_NE(readFile(path).find("flush visible"), std::string::npos);

    resetLogger(path);
}

TEST(LoggerTest, DisabledLoggerDoesNotWrite)
{
    std::string path = makeLogPath("disabled.log");
    ASSERT_TRUE(tinyrpc::Logger::init(path, tinyrpc::LogLevel::Debug));
    tinyrpc::Logger::setEnabled(false);

    ErrorLog("should not be written");
    tinyrpc::Logger::flush();

    EXPECT_EQ(readFile(path), "");

    resetLogger(path);
}

TEST(LoggerTest, AsyncModeEventuallyFlushes)
{
    std::string path = makeLogPath("async.log");
    ASSERT_TRUE(tinyrpc::Logger::init(path, tinyrpc::LogLevel::Debug, true));

    InfoLog("async one");
    WarnLog("async two");
    tinyrpc::Logger::flush();

    std::string content = readFile(path);
    EXPECT_NE(content.find("async one"), std::string::npos);
    EXPECT_NE(content.find("async two"), std::string::npos);

    resetLogger(path);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[log] PASS" << std::endl;
    }
    return result;
}
