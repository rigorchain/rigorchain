// Copyright (c) 2019-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <init/common.h>
#include <logging.h>
#include <logging/timer.h>
#include <test/util/setup_common.h>
#include <util/string.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(logging_tests, BasicTestingSetup)

static void ResetLogger()
{
    LogInstance().SetLogLevel(BCLog::DEFAULT_LOG_LEVEL);
    LogInstance().SetCategoryLogLevel({});
}

struct LogSetup : public BasicTestingSetup {
    fs::path prev_log_path;
    fs::path tmp_log_path;
    bool prev_reopen_file;
    bool prev_print_to_file;
    bool prev_log_timestamps;
    bool prev_log_threadnames;
    bool prev_log_sourcelocations;
    std::unordered_map<BCLog::LogFlags, BCLog::Level> prev_category_levels;
    BCLog::Level prev_log_level;

    LogSetup() : prev_log_path{LogInstance().m_file_path},
                 tmp_log_path{m_args.GetDataDirBase() / "tmp_debug.log"},
                 prev_reopen_file{LogInstance().m_reopen_file},
                 prev_print_to_file{LogInstance().m_print_to_file},
                 prev_log_timestamps{LogInstance().m_log_timestamps},
                 prev_log_threadnames{LogInstance().m_log_threadnames},
                 prev_log_sourcelocations{LogInstance().m_log_sourcelocations},
                 prev_category_levels{LogInstance().CategoryLevels()},
                 prev_log_level{LogInstance().LogLevel()}
    {
        LogInstance().m_file_path = tmp_log_path;
        LogInstance().m_reopen_file = true;
        LogInstance().m_print_to_file = true;
        LogInstance().m_log_timestamps = false;
        LogInstance().m_log_threadnames = false;

        // Prevent tests from failing when the line number of the logs changes.
        LogInstance().m_log_sourcelocations = false;

        LogInstance().SetLogLevel(BCLog::Level::Debug);
        LogInstance().SetCategoryLogLevel({});
    }

    ~LogSetup()
    {
        LogInstance().m_file_path = prev_log_path;
        LogPrintf("Sentinel log to reopen log file\n");
        LogInstance().m_print_to_file = prev_print_to_file;
        LogInstance().m_reopen_file = prev_reopen_file;
        LogInstance().m_log_timestamps = prev_log_timestamps;
        LogInstance().m_log_threadnames = prev_log_threadnames;
        LogInstance().m_log_sourcelocations = prev_log_sourcelocations;
        LogInstance().SetLogLevel(prev_log_level);
        LogInstance().SetCategoryLogLevel(prev_category_levels);
    }
};

BOOST_AUTO_TEST_CASE(logging_timer)
{
    auto micro_timer = BCLog::Timer<std::chrono::microseconds>("tests", "end_msg");
    const std::string_view result_prefix{"tests: msg ("};
    BOOST_CHECK_EQUAL(micro_timer.LogMsg("msg").substr(0, result_prefix.size()), result_prefix);
}

BOOST_FIXTURE_TEST_CASE(logging_LogPrintf_, LogSetup)
{
    LogInstance().m_log_sourcelocations = true;
    LogPrintf_("fn1", "src1", 1, BCLog::LogFlags::NET, BCLog::Level::Debug, "foo1: %s", "bar1\n");
    LogPrintf_("fn2", "src2", 2, BCLog::LogFlags::NET, BCLog::Level::None, "foo2: %s", "bar2\n");
    LogPrintf_("fn3", "src3", 3, BCLog::LogFlags::NONE, BCLog::Level::Debug, "foo3: %s", "bar3\n");
    LogPrintf_("fn4", "src4", 4, BCLog::LogFlags::NONE, BCLog::Level::None, "foo4: %s", "bar4\n");
    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    std::vector<std::string> expected = {
        "[src1:1] [fn1] [net:debug] foo1: bar1",
        "[src2:2] [fn2] [net] foo2: bar2",
        "[src3:3] [fn3] [debug] foo3: bar3",
        "[src4:4] [fn4] foo4: bar4",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_LogPrintMacros, LogSetup)
{
    LogPrintf("foo5: %s\n", "bar5");
    LogPrint(BCLog::NET, "foo6: %s\n", "bar6");
    LogPrintLevel(BCLog::NET, BCLog::Level::Debug, "foo7: %s\n", "bar7");
    LogPrintLevel(BCLog::NET, BCLog::Level::Info, "foo8: %s\n", "bar8");
    LogPrintLevel(BCLog::NET, BCLog::Level::Warning, "foo9: %s\n", "bar9");
    LogPrintLevel(BCLog::NET, BCLog::Level::Error, "foo10: %s\n", "bar10");
    LogPrintfCategory(BCLog::VALIDATION, "foo11: %s\n", "bar11");
    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    std::vector<std::string> expected = {
        "foo5: bar5",
        "[net] foo6: bar6",
        "[net:debug] foo7: bar7",
        "[net:info] foo8: bar8",
        "[net:warning] foo9: bar9",
        "[net:error] foo10: bar10",
        "[validation] foo11: bar11",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_LogPrintMacros_CategoryName, LogSetup)
{
    LogInstance().EnableCategory(BCLog::LogFlags::ALL);
    const auto concatenated_category_names = LogInstance().LogCategoriesString();
    std::vector<std::pair<BCLog::LogFlags, std::string>> expected_category_names;
    const auto category_names = SplitString(concatenated_category_names, ',');
    for (const auto& category_name : category_names) {
        BCLog::LogFlags category;
        const auto trimmed_category_name = TrimString(category_name);
        BOOST_REQUIRE(GetLogCategory(category, trimmed_category_name));
        expected_category_names.emplace_back(category, trimmed_category_name);
    }

    std::vector<std::string> expected;
    for (const auto& [category, name] : expected_category_names) {
        LogPrint(category, "foo: %s\n", "bar");
        std::string expected_log = "[";
        expected_log += name;
        expected_log += "] foo: bar";
        expected.push_back(expected_log);
    }

    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_SeverityLevels, LogSetup)
{
    LogInstance().EnableCategory(BCLog::LogFlags::ALL);

    LogInstance().SetLogLevel(BCLog::Level::Debug);
    LogInstance().SetCategoryLogLevel(/*category_str=*/"net", /*level_str=*/"info");

    // Global log level
    LogPrintLevel(BCLog::HTTP, BCLog::Level::Info, "foo1: %s\n", "bar1");
    LogPrintLevel(BCLog::MEMPOOL, BCLog::Level::Trace, "foo2: %s. This log level is lower than the global one.\n", "bar2");
    LogPrintLevel(BCLog::VALIDATION, BCLog::Level::Warning, "foo3: %s\n", "bar3");
    LogPrintLevel(BCLog::RPC, BCLog::Level::Error, "foo4: %s\n", "bar4");

    // Category-specific log level
    LogPrintLevel(BCLog::NET, BCLog::Level::Warning, "foo5: %s\n", "bar5");
    LogPrintLevel(BCLog::NET, BCLog::Level::Debug, "foo6: %s. This log level is the same as the global one but lower than the category-specific one, which takes precedence. \n", "bar6");
    LogPrintLevel(BCLog::NET, BCLog::Level::Error, "foo7: %s\n", "bar7");

    std::vector<std::string> expected = {
        "[http:info] foo1: bar1",
        "[validation:warning] foo3: bar3",
        "[rpc:error] foo4: bar4",
        "[net:warning] foo5: bar5",
        "[net:error] foo7: bar7",
    };
    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_Conf, LogSetup)
{
    // Set global log level
    {
        ResetLogger();
        ArgsManager args;
        args.AddArg("-loglevel", "...", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        const char* argv_test[] = {"rigord", "-loglevel=debug"};
        std::string err;
        BOOST_REQUIRE(args.ParseParameters(2, argv_test, err));
        init::SetLoggingLevel(args);
        BOOST_CHECK_EQUAL(LogInstance().LogLevel(), BCLog::Level::Debug);
    }

    // Set category-specific log level
    {
        ResetLogger();
        ArgsManager args;
        args.AddArg("-loglevel", "...", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        const char* argv_test[] = {"rigord", "-loglevel=net:trace"};
        std::string err;
        BOOST_REQUIRE(args.ParseParameters(2, argv_test, err));
        init::SetLoggingLevel(args);
        BOOST_CHECK_EQUAL(LogInstance().LogLevel(), BCLog::DEFAULT_LOG_LEVEL);

        const auto& category_levels{LogInstance().CategoryLevels()};
        const auto net_it{category_levels.find(BCLog::LogFlags::NET)};
        BOOST_REQUIRE(net_it != category_levels.end());
        BOOST_CHECK_EQUAL(net_it->second, BCLog::Level::Trace);
    }

    // Set both global log level and category-specific log level
    {
        ResetLogger();
        ArgsManager args;
        args.AddArg("-loglevel", "...", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        const char* argv_test[] = {"rigord", "-loglevel=debug", "-loglevel=net:trace", "-loglevel=http:info"};
        std::string err;
        BOOST_REQUIRE(args.ParseParameters(4, argv_test, err));
        init::SetLoggingLevel(args);
        BOOST_CHECK_EQUAL(LogInstance().LogLevel(), BCLog::Level::Debug);

        const auto& category_levels{LogInstance().CategoryLevels()};
        BOOST_CHECK_EQUAL(category_levels.size(), 2);

        const auto net_it{category_levels.find(BCLog::LogFlags::NET)};
        BOOST_CHECK(net_it != category_levels.end());
        BOOST_CHECK_EQUAL(net_it->second, BCLog::Level::Trace);

        const auto http_it{category_levels.find(BCLog::LogFlags::HTTP)};
        BOOST_CHECK(http_it != category_levels.end());
        BOOST_CHECK_EQUAL(http_it->second, BCLog::Level::Info);
    }
}

BOOST_FIXTURE_TEST_CASE(attacker_controlled_logs_are_rate_limited, LogSetup)
{
    ResetAttackerLogRateLimitForTest();
    SetAttackerLogQuotaForTest(40);

    LogPrintf("connected to self at %s, disconnecting\n", "127.0.0.1:8333");
    LogPrintf("connected to self at %s, disconnecting\n", "127.0.0.1:8334");
    LogPrintf("connected to self at %s, disconnecting\n", "127.0.0.1:8335");
    LogPrintf_("InvalidChainFound", __FILE__, __LINE__, BCLog::LogFlags::NONE, BCLog::Level::None, "Invalid chain: bad-block=%s\n", "aa");
    LogPrintf_("InvalidChainFound", __FILE__, __LINE__, BCLog::LogFlags::NONE, BCLog::Level::None, "Invalid chain: bad-block=%s\n", "bb");
    LogPrintf_("InvalidChainFound", __FILE__, __LINE__, BCLog::LogFlags::NONE, BCLog::Level::None, "Invalid chain: bad-block=%s\n", "cc");
    for (int i = 0; i < 4; ++i) {
        LogPrintf("ordinary operator note %d\n", i);
    }

    std::ifstream file{tmp_log_path};
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto count = [&](const std::string& needle) {
        size_t found = 0;
        for (size_t pos = 0; (pos = contents.find(needle, pos)) != std::string::npos; pos += needle.size()) {
            ++found;
        }
        return found;
    };
    BOOST_CHECK_EQUAL(count("connected to self"), 1U);
    BOOST_CHECK_EQUAL(count("Invalid chain:"), 1U);
    BOOST_CHECK_EQUAL(count("ordinary operator note"), 4U);
    BOOST_CHECK(contents.find("rate-limited attacker-controlled log") != std::string::npos);
    ResetAttackerLogRateLimitForTest();
}

BOOST_FIXTURE_TEST_CASE(attacker_header_logs_are_rate_limited, LogSetup)
{
    ResetAttackerLogRateLimitForTest();
    SetAttackerLogQuotaForTest(40);

    const char* bad_diffbits = "ERROR: bad-diffbits: got=0x%08x expect=0x%08x nTime=%u height=%d\n";
    const char* checkpoint_fork = "ERROR: %s: forked chain older than last checkpoint (height %d)\n";
    LogPrintf(bad_diffbits, 1u, 2u, 3u, 4);
    LogPrintf(bad_diffbits, 1u, 2u, 3u, 5);
    LogPrintf(bad_diffbits, 1u, 2u, 3u, 6);
    LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 1);
    LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 2);
    LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 3);
    LogPrintf("ordinary operator note %d\n", 9);

    const auto count_in = [](const std::string& contents, const std::string& needle) {
        size_t found = 0;
        for (size_t pos = 0; (pos = contents.find(needle, pos)) != std::string::npos; pos += needle.size()) {
            ++found;
        }
        return found;
    };
    std::ifstream file{tmp_log_path};
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    BOOST_CHECK_EQUAL(count_in(contents, "ERROR: bad-diffbits"), 1U);
    BOOST_CHECK_EQUAL(count_in(contents, "forked chain older than last checkpoint"), 1U);
    BOOST_CHECK_EQUAL(count_in(contents, "ordinary operator note"), 1U);

    ResetAttackerLogRateLimitForTest();
    SetAttackerLogQuotaForTest(40);
    LogPrintf(bad_diffbits, 1u, 2u, 3u, 7);
    LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 4);
    LogPrintf(bad_diffbits, 1u, 2u, 3u, 8);
    LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 5);

    std::ifstream after{tmp_log_path};
    std::string recovered((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());
    BOOST_CHECK_EQUAL(count_in(recovered, "ERROR: bad-diffbits"), 2U);
    BOOST_CHECK_EQUAL(count_in(recovered, "forked chain older than last checkpoint"), 2U);
    BOOST_CHECK_EQUAL(count_in(recovered, "ordinary operator note"), 1U);
    ResetAttackerLogRateLimitForTest();
}

BOOST_FIXTURE_TEST_CASE(attacker_log_quotas_are_isolated, LogSetup)
{
    const char* bad_diffbits = "ERROR: bad-diffbits: got=0x%08x expect=0x%08x nTime=%u height=%d\n";
    const char* checkpoint_fork = "ERROR: %s: forked chain older than last checkpoint (height %d)\n";
    const auto read_log = [&]() {
        std::ifstream file{tmp_log_path};
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };
    const auto count_in = [](const std::string& contents, const std::string& needle) {
        size_t found = 0;
        for (size_t pos = 0; (pos = contents.find(needle, pos)) != std::string::npos; pos += needle.size()) {
            ++found;
        }
        return found;
    };
    size_t baseline = read_log().size();
    const auto fresh = [&]() {
        const std::string contents = read_log();
        return contents.size() >= baseline ? contents.substr(baseline) : contents;
    };

    ResetAttackerLogRateLimitForTest();
    BOOST_CHECK_EQUAL(AttackerLogPeerQuotaForTest(), static_cast<size_t>(1024 * 1024));
    BOOST_CHECK_EQUAL(AttackerLogGlobalQuotaForTest(), static_cast<size_t>(ATTACKER_LOG_GLOBAL_MULTIPLIER * 1024 * 1024));
    LogPrintf("Error reading from database: phase2j isolation check\n");
    LogPrintf("ERROR: ConnectBlock(): phase2j invariant check\n");
    BOOST_CHECK_EQUAL(count_in(fresh(), "Error reading from database: phase2j"), 1U);
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: ConnectBlock(): phase2j"), 1U);

    SetAttackerLogQuotaForTest(40);
    {
        AttackerLogPeerScope peer_a{1};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 1);
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 2);
    }
    {
        AttackerLogPeerScope peer_b{2};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 3);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 2U);
    BOOST_CHECK_EQUAL(AttackerLogSuppressedCountForTest("bad-diffbits", 1), 1U);

    {
        AttackerLogPeerScope peer_a{1};
        LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 1);
        LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 2);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "forked chain older than last checkpoint"), 1U);
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 2U);

    ResetAttackerLogRateLimitForTest();
    baseline = read_log().size();
    SetAttackerLogQuotaForTest(1024 * 1024);
    SetAttackerLogGlobalQuotaForTest(40);
    {
        AttackerLogPeerScope peer_a{4};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 4);
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 5);
    }
    {
        AttackerLogPeerScope peer_b{5};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 6);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 1U);
    {
        AttackerLogPeerScope peer_b{5};
        LogPrintf(checkpoint_fork, "ContextualCheckBlockHeader", 9);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "forked chain older than last checkpoint"), 1U);
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 1U);

    ResetAttackerLogRateLimitForTest();
    baseline = read_log().size();
    SetAttackerLogQuotaForTest(40);
    {
        AttackerLogPeerScope peer_a{6};
        for (int i = 0; i < 20; ++i) {
            LogPrintf(bad_diffbits, 1u, 2u, 3u, i);
        }
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 1U);
    BOOST_CHECK_EQUAL(count_in(fresh(), "rate-limited attacker-controlled log"), 1U);
    BOOST_CHECK_EQUAL(AttackerLogSuppressedCountForTest("bad-diffbits", 6), 19U);
    ExpireAttackerLogWindowsForTest();
    {
        AttackerLogPeerScope peer_a{6};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 20);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "suppressed 19 messages in the previous window"), 1U);
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 2U);
    BOOST_CHECK_EQUAL(count_in(fresh(), "rate-limited attacker-controlled log"), 1U);

    ResetAttackerLogRateLimitForTest();
    baseline = read_log().size();
    SetAttackerLogQuotaForTest(40);
    {
        AttackerLogPeerScope peer_a{7};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 7);
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 8);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 1U);
    BOOST_CHECK(AttackerLogPeerSlotCountForTest() >= 1U);
    ReleaseAttackerLogPeer(7);
    BOOST_CHECK_EQUAL(count_in(fresh(), "suppressed 1 messages in the previous window"), 1U);
    BOOST_CHECK_EQUAL(AttackerLogPeerSlotCountForTest(), 0U);
    BOOST_CHECK_EQUAL(AttackerLogSuppressedCountForTest("bad-diffbits", 7), 0U);
    {
        AttackerLogPeerScope peer_a{7};
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 9);
        LogPrintf(bad_diffbits, 1u, 2u, 3u, 10);
    }
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 2U);
    BOOST_CHECK_EQUAL(AttackerLogSuppressedCountForTest("bad-diffbits", 7), 1U);

    ResetAttackerLogRateLimitForTest();
    for (int64_t id = 1000; id < 1200; ++id) {
        AttackerLogPeerScope scope{id};
        LogPrintf(bad_diffbits, 1u, 2u, static_cast<unsigned>(id), 1);
    }
    BOOST_CHECK_EQUAL(AttackerLogPeerSlotCountForTest(), MAX_ATTACKER_LOG_PEERS);
    for (int64_t id = 1000; id < 1200; ++id) {
        ReleaseAttackerLogPeer(id);
    }
    BOOST_CHECK_EQUAL(AttackerLogPeerSlotCountForTest(), 0U);

    ResetAttackerLogRateLimitForTest();
    baseline = read_log().size();
    SetAttackerLogQuotaForTest(40);
    std::thread peer_one{[&]() {
        AttackerLogPeerScope scope{50};
        for (int i = 0; i < 30; ++i) {
            LogPrintf(bad_diffbits, 1u, 2u, 3u, i);
        }
    }};
    std::thread peer_two{[&]() {
        AttackerLogPeerScope scope{51};
        for (int i = 0; i < 30; ++i) {
            LogPrintf(bad_diffbits, 1u, 2u, 3u, i);
        }
    }};
    peer_one.join();
    peer_two.join();
    BOOST_CHECK_EQUAL(count_in(fresh(), "ERROR: bad-diffbits"), 2U);
    BOOST_CHECK_EQUAL(AttackerLogSuppressedCountForTest("bad-diffbits", 50), 29U);
    BOOST_CHECK_EQUAL(AttackerLogSuppressedCountForTest("bad-diffbits", 51), 29U);
    BOOST_CHECK_EQUAL(AttackerLogPeerSlotCountForTest(), 2U);
    ResetAttackerLogRateLimitForTest();
}

BOOST_AUTO_TEST_SUITE_END()
