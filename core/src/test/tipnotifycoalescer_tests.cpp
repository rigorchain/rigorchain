// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/tipnotifycoalescer.h>
#include <chain.h>
#include <test/util/setup_common.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(tipnotifycoalescer_tests, BasicTestingSetup)

namespace {

TipNotifyCoalescer::Snapshot BlockTip(int height, int64_t block_time, double progress, int sync_state)
{
    TipNotifyCoalescer::Snapshot snap;
    snap.height = height;
    snap.block_time = block_time;
    snap.progress = progress;
    snap.sync_type = TIPNOTIFY_SYNC_BLOCK;
    snap.sync_state = sync_state;
    return snap;
}

TipNotifyCoalescer::Snapshot HeaderTip(int height, int sync_state)
{
    TipNotifyCoalescer::Snapshot snap;
    snap.height = height;
    snap.block_time = 0;
    snap.progress = 0.0;
    snap.sync_type = TIPNOTIFY_SYNC_HEADER_SYNC;
    snap.sync_state = sync_state;
    return snap;
}

struct Paint {
    bool emit_now{false};
    bool schedule{false};
    int height{0};
};

Paint Feed(TipNotifyCoalescer& coalescer, const TipNotifyCoalescer::Snapshot& snap, bool throttle, int64_t now_ms)
{
    const auto decision = coalescer.ObserveBlock(snap, throttle, now_ms);
    Paint paint;
    paint.emit_now = decision.emit_now;
    paint.schedule = decision.schedule;
    if (decision.emit_now) paint.height = decision.emit_snapshot.height;
    return paint;
}

} // namespace

BOOST_AUTO_TEST_CASE(normal_throttling_keeps_one_paint_per_window)
{
    TipNotifyCoalescer coalescer{250};
    int paints = 0;
    int schedules = 0;
    for (int height = 1; height <= 20; ++height) {
        const Paint paint = Feed(coalescer, BlockTip(height, 1'000 + height, 0.5, TIPNOTIFY_STATE_INIT_DOWNLOAD), true, /*now_ms=*/height);
        if (paint.emit_now) {
            ++paints;
            BOOST_CHECK_EQUAL(paint.height, 1);
        }
        if (paint.schedule) ++schedules;
    }
    BOOST_CHECK_EQUAL(paints, 1);
    BOOST_CHECK_EQUAL(schedules, 1);

    // The next window may paint again. The first paint was at t=1, so t=251 is the first legal instant.
    const Paint later = Feed(coalescer, BlockTip(21, 2'000, 0.5, TIPNOTIFY_STATE_INIT_DOWNLOAD), true, /*now_ms=*/251);
    BOOST_CHECK(later.emit_now);
    BOOST_CHECK(!later.schedule);
    BOOST_CHECK_EQUAL(later.height, 21);
}

BOOST_AUTO_TEST_CASE(trailing_flush_delivers_newest_tip_without_another_block)
{
    // Restart paints the on-disk tip once from AppInitMain, outside TipChanged, so a
    // reopen can show 1619 with no new block. This case is the same recovery while
    // the process stays up: the suppressed tip is delivered by the trailing flush.
    TipNotifyCoalescer coalescer{250};
    const auto first = coalescer.ObserveBlock(BlockTip(1607, 1'793'245'534, 1.0, TIPNOTIFY_STATE_POST_INIT), true, /*now_ms=*/0);
    BOOST_CHECK(first.emit_now);
    BOOST_CHECK_EQUAL(first.emit_snapshot.height, 1607);

    const auto suppressed = coalescer.ObserveBlock(BlockTip(1619, 1'793'250'474, 1.0, TIPNOTIFY_STATE_POST_INIT), true, /*now_ms=*/10);
    BOOST_CHECK(!suppressed.emit_now);
    BOOST_CHECK(suppressed.schedule);
    BOOST_CHECK_EQUAL(suppressed.delay_ms, 240);

    // No further Observe. The armed flush is the only way the GUI hears about 1619.
    const auto flushed = coalescer.FlushBlock(/*now_ms=*/250);
    BOOST_REQUIRE(flushed.has_value());
    BOOST_CHECK_EQUAL(flushed->height, 1619);
    BOOST_CHECK_EQUAL(flushed->block_time, static_cast<int64_t>(1'793'250'474));
    BOOST_CHECK_CLOSE(flushed->progress, 1.0, 0.0001);
    BOOST_CHECK_EQUAL(flushed->sync_state, TIPNOTIFY_STATE_POST_INIT);
    BOOST_CHECK(!coalescer.FlushBlock(/*now_ms=*/250).has_value());
}

BOOST_AUTO_TEST_CASE(rapid_burst_paints_the_latest_height_and_zero_remaining)
{
    // Windows incident: headers and blocks 1595..1619 arrived in about one second.
    // The overlay kept height 1607 (12 blocks short of header tip 1619).
    constexpr int kHeaderTip = 1619;
    TipNotifyCoalescer coalescer{250};
    int paints = 0;
    int schedules = 0;
    int last_painted = -1;
    for (int height = 1595; height <= kHeaderTip; ++height) {
        const int64_t now_ms = height == 1595 ? 0 : 1;
        const Paint paint = Feed(coalescer, BlockTip(height, 1'793'240'000 + height, height == kHeaderTip ? 1.0 : 0.99, TIPNOTIFY_STATE_POST_INIT), true, now_ms);
        if (paint.emit_now) {
            ++paints;
            last_painted = paint.height;
        }
        if (paint.schedule) ++schedules;
    }
    BOOST_CHECK_EQUAL(paints, 1);
    BOOST_CHECK_EQUAL(schedules, 1);
    BOOST_CHECK_EQUAL(last_painted, 1595);
    BOOST_CHECK_EQUAL(kHeaderTip - last_painted, 24);

    const auto flushed = coalescer.FlushBlock(/*now_ms=*/250);
    BOOST_REQUIRE(flushed.has_value());
    BOOST_CHECK_EQUAL(flushed->height, kHeaderTip);
    // ModalOverlay::tipUpdate shows bestHeaderHeight - count once the block signal catches the header tip.
    BOOST_CHECK_EQUAL(kHeaderTip - flushed->height, 0);
    BOOST_CHECK_CLOSE(flushed->progress, 1.0, 0.0001);
}

BOOST_AUTO_TEST_CASE(completion_payload_is_fresh_enough_to_clear_the_catchup_bar)
{
    // Block 1619 time from the running wallet, and the wall clock when it was inspected.
    // setNumBlocks hides the catch-up bar when the painted block is younger than MAX_BLOCK_TIME_GAP.
    // 2026-10-29T03:45:34Z (block 1607) and 2026-10-29T05:07:54Z (block 1619).
    constexpr int64_t kStaleTime = 1'793'245'534;
    constexpr int64_t kTipTime = 1'793'250'474;
    constexpr int64_t kWallClock = 1'793'253'593;
    TipNotifyCoalescer coalescer{250};
    BOOST_CHECK(Feed(coalescer, BlockTip(1607, kStaleTime, 1.0, TIPNOTIFY_STATE_POST_INIT), true, 0).emit_now);
    BOOST_CHECK(!Feed(coalescer, BlockTip(1619, kTipTime, 1.0, TIPNOTIFY_STATE_POST_INIT), true, 40).emit_now);

    const auto flushed = coalescer.FlushBlock(250);
    BOOST_REQUIRE(flushed.has_value());
    BOOST_CHECK_EQUAL(flushed->sync_state, TIPNOTIFY_STATE_POST_INIT);
    BOOST_CHECK_EQUAL(flushed->sync_type, TIPNOTIFY_SYNC_BLOCK);
    BOOST_CHECK_LT(kWallClock - flushed->block_time, MAX_BLOCK_TIME_GAP);
    BOOST_CHECK_GE(kWallClock - kStaleTime, MAX_BLOCK_TIME_GAP);
}

BOOST_AUTO_TEST_CASE(reindex_throttles_without_inventing_completion)
{
    TipNotifyCoalescer coalescer{250};
    int header_paints = 0;
    int header_schedules = 0;
    for (int height = 1; height <= 30; ++height) {
        const auto decision = coalescer.ObserveHeader(HeaderTip(height, TIPNOTIFY_STATE_INIT_REINDEX), true, height);
        if (decision.emit_now) {
            ++header_paints;
            BOOST_CHECK_EQUAL(decision.emit_snapshot.sync_state, TIPNOTIFY_STATE_INIT_REINDEX);
        }
        if (decision.schedule) ++header_schedules;
    }
    BOOST_CHECK_EQUAL(header_paints, 1);
    BOOST_CHECK_EQUAL(header_schedules, 1);

    const auto flushed = coalescer.FlushHeader(250);
    BOOST_REQUIRE(flushed.has_value());
    BOOST_CHECK_EQUAL(flushed->height, 30);
    BOOST_CHECK_EQUAL(flushed->sync_state, TIPNOTIFY_STATE_INIT_REINDEX);
    BOOST_CHECK_EQUAL(flushed->sync_type, TIPNOTIFY_SYNC_HEADER_SYNC);

    // Outside reindex, header tips are not throttled and must not wait for a flush.
    TipNotifyCoalescer live{250};
    for (int height = 1; height <= 5; ++height) {
        const auto decision = live.ObserveHeader(HeaderTip(height, TIPNOTIFY_STATE_POST_INIT), false, height);
        BOOST_CHECK(decision.emit_now);
        BOOST_CHECK(!decision.schedule);
        BOOST_CHECK_EQUAL(decision.emit_snapshot.height, height);
        BOOST_CHECK_EQUAL(decision.emit_snapshot.sync_state, TIPNOTIFY_STATE_POST_INIT);
    }
    BOOST_CHECK(!live.FlushHeader(10).has_value());
}

BOOST_AUTO_TEST_CASE(one_flush_is_armed_and_dropped_state_is_not_replayed)
{
    constexpr int64_t kDelay = 250;
    int schedules = 0;
    std::optional<TipNotifyCoalescer::Snapshot> delivered;
    {
        TipNotifyCoalescer coalescer{kDelay};
        for (int i = 0; i < 1000; ++i) {
            const auto decision = coalescer.ObserveBlock(BlockTip(i + 1, i, 1.0, TIPNOTIFY_STATE_POST_INIT), true, i == 0 ? 0 : 1);
            if (decision.schedule) ++schedules;
        }
        BOOST_CHECK_EQUAL(schedules, 1);
        // Destroying the coalescer drops the pending snapshot with it.
        // ClientModel binds the timer to the QObject, so this is also what a
        // destroyed model does: the callback is not invoked and nothing is replayed.
    }
    TipNotifyCoalescer replacement{kDelay};
    BOOST_CHECK(!replacement.FlushBlock(kDelay).has_value());
    const auto decision = replacement.ObserveBlock(BlockTip(1, 1, 1.0, TIPNOTIFY_STATE_POST_INIT), true, 0);
    BOOST_CHECK(decision.emit_now);
    BOOST_CHECK(!decision.schedule);
    delivered = decision.emit_snapshot;
    BOOST_CHECK_EQUAL(delivered->height, 1);

    // A later in-window update must not enqueue a second flush, and an update
    // that is allowed to paint immediately must clear the pending snapshot so
    // the already-armed flush does not emit a duplicate.
    TipNotifyCoalescer coalescer{kDelay};
    BOOST_CHECK(coalescer.ObserveBlock(BlockTip(1, 1, 1.0, TIPNOTIFY_STATE_POST_INIT), true, 0).emit_now);
    BOOST_CHECK(coalescer.ObserveBlock(BlockTip(2, 2, 1.0, TIPNOTIFY_STATE_POST_INIT), true, 10).schedule);
    BOOST_CHECK(!coalescer.ObserveBlock(BlockTip(3, 3, 1.0, TIPNOTIFY_STATE_POST_INIT), true, 20).schedule);
    const auto immediate = coalescer.ObserveBlock(BlockTip(4, 4, 1.0, TIPNOTIFY_STATE_POST_INIT), true, kDelay);
    BOOST_CHECK(immediate.emit_now);
    BOOST_CHECK_EQUAL(immediate.emit_snapshot.height, 4);
    BOOST_CHECK(!coalescer.FlushBlock(kDelay).has_value());
}

// Unmodified Bitcoin Core v25.0 TipChanged control flow.
// src/qt/clientmodel.cpp at 8105bce5b384c72cf08b25b7c5343622754e7337 (tag v25.0).
// No trailing flush. Throttle is only IBD block-sync or reindex.
struct UpstreamV25TipChanged {
    int64_t last_block{0};
    int64_t last_header{0};
    std::vector<int> painted;
    void OnTip(int sync_state, int sync_type, int height, int64_t now_ms)
    {
        const bool throttle = (sync_state != TIPNOTIFY_STATE_POST_INIT && sync_type == TIPNOTIFY_SYNC_BLOCK) ||
                              sync_state == TIPNOTIFY_STATE_INIT_REINDEX;
        const int64_t now = throttle ? now_ms : 0;
        int64_t& last = sync_type != TIPNOTIFY_SYNC_BLOCK ? last_header : last_block;
        if (throttle && now < last + 250) return;
        painted.push_back(height);
        last = now;
    }
};

// RIGOR predicate introduced in 50c46f2f, before the trailing flush.
// BLOCK_SYNC is throttled in every sync state, including POST_INIT.
struct RigorPreFixTipChanged {
    int64_t last_block{0};
    int64_t last_header{0};
    std::vector<int> painted;
    void OnTip(int sync_state, int sync_type, int height, int64_t now_ms)
    {
        const bool throttle = sync_type == TIPNOTIFY_SYNC_BLOCK || sync_state == TIPNOTIFY_STATE_INIT_REINDEX;
        const int64_t now = now_ms;
        int64_t& last = sync_type != TIPNOTIFY_SYNC_BLOCK ? last_header : last_block;
        if (throttle && now < last + 250) return;
        painted.push_back(height);
        last = now;
    }
};

template <typename Notifier>
std::vector<int> BurstThenSilence(int sync_state, int sync_type)
{
    // Static last-paint timestamps start at 0. A real GetTimeMillis() is far above 250,
    // so the first tip is painted and everything inside the next 250 ms is the burst.
    constexpr int64_t kNow = 10'000;
    Notifier notifier;
    notifier.OnTip(sync_state, sync_type, 1595, kNow);
    for (int height = 1596; height <= 1619; ++height) {
        notifier.OnTip(sync_state, sync_type, height, kNow + 1);
    }
    return notifier.painted;
}

BOOST_AUTO_TEST_CASE(upstream_v25_post_init_burst_emits_the_final_height)
{
    const std::vector<int> painted = BurstThenSilence<UpstreamV25TipChanged>(TIPNOTIFY_STATE_POST_INIT, TIPNOTIFY_SYNC_BLOCK);
    BOOST_REQUIRE_EQUAL(painted.size(), 25);
    BOOST_CHECK_EQUAL(painted.front(), 1595);
    BOOST_CHECK_EQUAL(painted.back(), 1619);
}

BOOST_AUTO_TEST_CASE(rigor_prefix_post_init_burst_drops_the_final_height)
{
    const std::vector<int> painted = BurstThenSilence<RigorPreFixTipChanged>(TIPNOTIFY_STATE_POST_INIT, TIPNOTIFY_SYNC_BLOCK);
    BOOST_REQUIRE_EQUAL(painted.size(), 1);
    BOOST_CHECK_EQUAL(painted.front(), 1595);
}

BOOST_AUTO_TEST_CASE(upstream_v25_ibd_burst_does_drop_the_tail)
{
    // The early return exists upstream, but only while initial sync or reindex is on.
    // The wallet incident had already left IBD, so this path was not the one that stuck.
    const std::vector<int> painted = BurstThenSilence<UpstreamV25TipChanged>(TIPNOTIFY_STATE_INIT_DOWNLOAD, TIPNOTIFY_SYNC_BLOCK);
    BOOST_REQUIRE_EQUAL(painted.size(), 1);
    BOOST_CHECK_EQUAL(painted.front(), 1595);
    const std::vector<int> reindex = BurstThenSilence<UpstreamV25TipChanged>(TIPNOTIFY_STATE_INIT_REINDEX, TIPNOTIFY_SYNC_HEADER_SYNC);
    BOOST_REQUIRE_EQUAL(reindex.size(), 1);
    BOOST_CHECK_EQUAL(reindex.front(), 1595);
}

BOOST_AUTO_TEST_SUITE_END()
