// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TIPNOTIFYFLUSH_H
#define BITCOIN_QT_TIPNOTIFYFLUSH_H

#include <QMetaObject>
#include <QObject>
#include <QTimer>

// Queue one trailing callback onto target's thread, then arm a single-shot
// whose context is target. Block-tip notifications arrive on the validation
// thread (no Qt event loop, and cs_main is held). Starting the timer there
// would drop the flush. Qt does not run either functor after target is destroyed.
template <typename Fn>
void ArmContextFlush(QObject* target, int msec, Fn fn)
{
    QMetaObject::invokeMethod(
        target, [target, msec, fn] {
            QTimer::singleShot(msec, target, fn);
        },
        Qt::QueuedConnection);
}

#endif // BITCOIN_QT_TIPNOTIFYFLUSH_H
