// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TIPNOTIFYCOALESCER_H
#define BITCOIN_QT_TIPNOTIFYCOALESCER_H

#include <cstdint>
#include <limits>
#include <optional>

// Numeric values must match qt/clientmodel.h SyncType and
// validation.h SynchronizationState. clientmodel.cpp static_asserts that.
inline constexpr int TIPNOTIFY_SYNC_HEADER_PRESYNC = 0;
inline constexpr int TIPNOTIFY_SYNC_HEADER_SYNC = 1;
inline constexpr int TIPNOTIFY_SYNC_BLOCK = 2;
inline constexpr int TIPNOTIFY_STATE_INIT_REINDEX = 0;
inline constexpr int TIPNOTIFY_STATE_INIT_DOWNLOAD = 1;
inline constexpr int TIPNOTIFY_STATE_POST_INIT = 2;

/**
 * Coalesce block-sync and header-sync GUI tip notifications.
 *
 * Intermediate updates inside the delay stay suppressed. The newest suppressed
 * snapshot is kept, and exactly one trailing flush is armed. Silence after a
 * burst still delivers that snapshot. There is no queue of notifications.
 *
 * Block and header channels are independent, matching the previous pair of
 * last-notification timestamps. The caller decides whether a given update is
 * subject to throttling (block sync, and any tip during reindex).
 *
 * The coalescer does not own a timer or a thread. The GUI model arms the
 * trailing callback on its own QObject so destruction cancels it.
 */
class TipNotifyCoalescer
{
public:
    struct Snapshot {
        int height{0};
        int64_t block_time{0};
        double progress{0.0};
        int sync_type{TIPNOTIFY_SYNC_BLOCK};
        int sync_state{TIPNOTIFY_STATE_POST_INIT};
    };

    struct Decision {
        bool emit_now{false};
        Snapshot emit_snapshot{};
        //! Arm one trailing flush. Stays false while a flush is already armed.
        bool schedule{false};
        int64_t delay_ms{0};
    };

    explicit TipNotifyCoalescer(int64_t delay_ms) : m_delay_ms(delay_ms) {}

    Decision ObserveBlock(const Snapshot& tip, bool throttle, int64_t now_ms)
    {
        return Observe(m_block, tip, throttle, now_ms);
    }
    Decision ObserveHeader(const Snapshot& tip, bool throttle, int64_t now_ms)
    {
        return Observe(m_header, tip, throttle, now_ms);
    }
    std::optional<Snapshot> FlushBlock(int64_t now_ms) { return Flush(m_block, now_ms); }
    std::optional<Snapshot> FlushHeader(int64_t now_ms) { return Flush(m_header, now_ms); }

private:
    struct Channel {
        int64_t last_emit_ms{std::numeric_limits<int64_t>::min()};
        bool flush_scheduled{false};
        bool has_pending{false};
        Snapshot pending{};
    };

    Decision Observe(Channel& channel, const Snapshot& tip, bool throttle, int64_t now_ms)
    {
        Decision decision;
        if (!throttle || now_ms >= channel.last_emit_ms + m_delay_ms) {
            channel.has_pending = false;
            channel.last_emit_ms = now_ms;
            decision.emit_now = true;
            decision.emit_snapshot = tip;
            return decision;
        }
        channel.pending = tip;
        channel.has_pending = true;
        if (!channel.flush_scheduled) {
            channel.flush_scheduled = true;
            decision.schedule = true;
            const int64_t wait = channel.last_emit_ms + m_delay_ms - now_ms;
            decision.delay_ms = wait > 0 ? wait : 0;
        }
        return decision;
    }

    std::optional<Snapshot> Flush(Channel& channel, int64_t now_ms)
    {
        channel.flush_scheduled = false;
        if (!channel.has_pending) return std::nullopt;
        Snapshot snap = channel.pending;
        channel.has_pending = false;
        channel.last_emit_ms = now_ms;
        return snap;
    }

    int64_t m_delay_ms;
    Channel m_block;
    Channel m_header;
};

#endif // BITCOIN_QT_TIPNOTIFYCOALESCER_H
