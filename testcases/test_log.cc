#include "comm/log.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
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

void removePrefixedLogs(const std::string& prefix)
{
    std::filesystem::create_directories("build/log-tests");
    for (const auto& entry : std::filesystem::directory_iterator("build/log-tests")) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            std::filesystem::remove(entry.path());
        }
    }
}

std::vector<std::filesystem::path> listPrefixedLogs(const std::string& prefix)
{
    std::vector<std::filesystem::path> paths;
    std::filesystem::create_directories("build/log-tests");
    for (const auto& entry : std::filesystem::directory_iterator("build/log-tests")) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

size_t countSubstring(const std::string& content, const std::string& needle)
{
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::string readPrefixedLogs(const std::string& prefix)
{
    std::string content;
    for (const auto& path : listPrefixedLogs(prefix)) {
        content += readFile(path.string());
    }
    return content;
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
        tinyrpc::LogType::RpcLog,
        tinyrpc::LogLevel::Error,
        "unit_file.cc",
        77,
        "unit_func",
        "method=QueryService.query_name err=100",
        "req-001"
    );
    tinyrpc::Logger::flush();

    std::string content = readFile(path);
    EXPECT_NE(content.find("[RPC]"), std::string::npos);
    EXPECT_NE(content.find("[ERROR]"), std::string::npos);
    EXPECT_NE(content.find("[pid="), std::string::npos);
    EXPECT_NE(content.find("[tid="), std::string::npos);
    EXPECT_NE(content.find("[co="), std::string::npos);
    EXPECT_NE(content.find("[unit_file.cc:77]"), std::string::npos);
    EXPECT_NE(content.find("[func=unit_func]"), std::string::npos);
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

TEST(LoggerTest, RpcAndAppLogsUseSeparateFiles)
{
    removePrefixedLogs("task88_separate");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task88_separate",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug
    ));

    InfoLog("rpc only message");
    AppInfoLog("app only message");
    tinyrpc::Logger::flush();

    std::string rpcContent = readFile("build/log-tests/task88_separate_rpc.log");
    std::string appContent = readFile("build/log-tests/task88_separate_app.log");
    EXPECT_NE(rpcContent.find("rpc only message"), std::string::npos);
    EXPECT_EQ(rpcContent.find("app only message"), std::string::npos);
    EXPECT_NE(appContent.find("app only message"), std::string::npos);
    EXPECT_EQ(appContent.find("rpc only message"), std::string::npos);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task88_separate");
}

TEST(LoggerTest, RpcAndAppLevelsFilterIndependently)
{
    removePrefixedLogs("task88_levels");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task88_levels",
        tinyrpc::LogLevel::Warn,
        tinyrpc::LogLevel::Debug
    ));

    InfoLog("rpc info skipped");
    WarnLog("rpc warn kept");
    AppInfoLog("app info kept");
    tinyrpc::Logger::flush();

    std::string rpcContent = readFile("build/log-tests/task88_levels_rpc.log");
    std::string appContent = readFile("build/log-tests/task88_levels_app.log");
    EXPECT_EQ(rpcContent.find("rpc info skipped"), std::string::npos);
    EXPECT_NE(rpcContent.find("rpc warn kept"), std::string::npos);
    EXPECT_NE(appContent.find("app info kept"), std::string::npos);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task88_levels");
}

TEST(LoggerTest, LogLineContainsEventFields)
{
    removePrefixedLogs("task88_fields");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task88_fields",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug
    ));

    tinyrpc::Logger::log(
        tinyrpc::LogType::RpcLog,
        tinyrpc::LogLevel::Info,
        "event_file.cc",
        123,
        "event_func",
        "event fields message",
        "event-req"
    );
    tinyrpc::Logger::flush();

    std::string content = readFile("build/log-tests/task88_fields_rpc.log");
    EXPECT_NE(content.find("[RPC]"), std::string::npos);
    EXPECT_NE(content.find("[INFO]"), std::string::npos);
    EXPECT_NE(content.find("[pid="), std::string::npos);
    EXPECT_NE(content.find("[tid="), std::string::npos);
    EXPECT_NE(content.find("[co="), std::string::npos);
    EXPECT_NE(content.find("[reqId=event-req]"), std::string::npos);
    EXPECT_NE(content.find("[event_file.cc:123]"), std::string::npos);
    EXPECT_NE(content.find("[func=event_func]"), std::string::npos);
    EXPECT_NE(content.find("event fields message"), std::string::npos);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task88_fields");
}

TEST(LoggerTest, AsyncModeFlushesRpcAndAppFiles)
{
    removePrefixedLogs("task89_async_flush");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task89_async_flush",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug,
        true,
        1000,
        64 * 1024 * 1024
    ));

    InfoLog("async rpc visible");
    AppInfoLog("async app visible");
    tinyrpc::Logger::flush();

    EXPECT_NE(readFile("build/log-tests/task89_async_flush_rpc.log").find("async rpc visible"), std::string::npos);
    EXPECT_NE(readFile("build/log-tests/task89_async_flush_app.log").find("async app visible"), std::string::npos);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task89_async_flush");
}

TEST(LoggerTest, AsyncModeConcurrentWritesDoNotLoseLines)
{
    removePrefixedLogs("task89_concurrent");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task89_concurrent",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug,
        true,
        1000,
        64 * 1024 * 1024
    ));

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 100; ++i) {
                InfoLog("concurrent line " + std::to_string(t) + "-" + std::to_string(i));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    tinyrpc::Logger::flush();

    std::string content = readFile("build/log-tests/task89_concurrent_rpc.log");
    EXPECT_EQ(countSubstring(content, "concurrent line "), 400);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task89_concurrent");
}

TEST(LoggerTest, ShutdownDrainsQueuedMessages)
{
    removePrefixedLogs("task89_shutdown_drain");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task89_shutdown_drain",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug,
        true,
        1000,
        64 * 1024 * 1024
    ));

    InfoLog("drain rpc before shutdown");
    AppInfoLog("drain app before shutdown");
    tinyrpc::Logger::shutdown();

    EXPECT_NE(readFile("build/log-tests/task89_shutdown_drain_rpc.log").find("drain rpc before shutdown"), std::string::npos);
    EXPECT_NE(readFile("build/log-tests/task89_shutdown_drain_app.log").find("drain app before shutdown"), std::string::npos);
    removePrefixedLogs("task89_shutdown_drain");
}

TEST(LoggerTest, ShutdownCanReinitializeLogger)
{
    removePrefixedLogs("task89_reinit_old");
    removePrefixedLogs("task89_reinit_new");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task89_reinit_old",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug,
        true,
        1000,
        64 * 1024 * 1024
    ));
    InfoLog("old before shutdown");
    tinyrpc::Logger::shutdown();
    auto oldSize = std::filesystem::file_size("build/log-tests/task89_reinit_old_rpc.log");

    InfoLog("console after shutdown");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task89_reinit_new",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug,
        false,
        1000,
        64 * 1024 * 1024
    ));
    InfoLog("new after reinit");
    tinyrpc::Logger::flush();

    EXPECT_EQ(std::filesystem::file_size("build/log-tests/task89_reinit_old_rpc.log"), oldSize);
    EXPECT_NE(readFile("build/log-tests/task89_reinit_new_rpc.log").find("new after reinit"), std::string::npos);
    EXPECT_EQ(readFile("build/log-tests/task89_reinit_old_rpc.log").find("console after shutdown"), std::string::npos);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task89_reinit_old");
    removePrefixedLogs("task89_reinit_new");
}

TEST(LoggerTest, SmallMaxSizeTriggersRolling)
{
    removePrefixedLogs("task89_roll");
    ASSERT_TRUE(tinyrpc::Logger::init(
        "build/log-tests",
        "task89_roll",
        tinyrpc::LogLevel::Debug,
        tinyrpc::LogLevel::Debug,
        false,
        1000,
        260
    ));

    for (int i = 0; i < 20; ++i) {
        InfoLog("rolling rpc line " + std::to_string(i) + " with payload payload payload");
        AppInfoLog("rolling app line " + std::to_string(i) + " with payload payload payload");
    }
    tinyrpc::Logger::flush();

    EXPECT_TRUE(std::filesystem::exists("build/log-tests/task89_roll_rpc.log"));
    EXPECT_TRUE(std::filesystem::exists("build/log-tests/task89_roll_rpc.log.1"));
    EXPECT_TRUE(std::filesystem::exists("build/log-tests/task89_roll_app.log"));
    EXPECT_TRUE(std::filesystem::exists("build/log-tests/task89_roll_app.log.1"));
    EXPECT_EQ(countSubstring(readPrefixedLogs("task89_roll_rpc"), "rolling rpc line "), 20);
    EXPECT_EQ(countSubstring(readPrefixedLogs("task89_roll_app"), "rolling app line "), 20);

    tinyrpc::Logger::shutdown();
    removePrefixedLogs("task89_roll");
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
