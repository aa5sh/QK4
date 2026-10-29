// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardware/keyerruntime.h"

#include <QMetaObject>
#include <QTimer>
#include <chrono>
#include <cmath>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

double SteadyKeyerClock::now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// WHY a per-thread high-resolution waitable timer on Windows: Sleep() rounds up to the 15.6 ms
// system tick, longer than a dit at 80 WPM. Same technique as the V1.4 worker's poll loop; falls
// back to Sleep() where the high-resolution flag is unsupported (pre-1803 Windows 10).
void SteadyKeyerClock::sleep(double seconds) {
    if (!(seconds > 0))
        return;
#ifdef Q_OS_WIN
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    thread_local HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(std::llround(seconds * 1e7)); // 100 ns units, relative
        if (due.QuadPart == 0)
            due.QuadPart = -1;
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    Sleep(static_cast<DWORD>(std::ceil(seconds * 1000)));
#else
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
#endif
}

void FunctionLink::setSender(Sender sender) {
    std::lock_guard lock(m_mutex);
    m_sender = std::move(sender);
}

void FunctionLink::send(const std::string &command) {
    Sender sender;
    {
        std::lock_guard lock(m_mutex);
        sender = m_sender;
    }
    if (sender)
        sender(command);
}

QtOneShotTimer::QtOneShotTimer(QObject *parent) : QObject(parent) {}

void QtOneShotTimer::start(double delaySeconds, std::function<void()> fire) {
    const quint64 generation = ++m_generation;
    const int ms = static_cast<int>(std::ceil(std::max(0.0, delaySeconds) * 1000));
    QMetaObject::invokeMethod(
        this,
        [this, generation, ms, fire = std::move(fire)]() {
            QTimer::singleShot(ms, Qt::PreciseTimer, this, [this, generation, fire]() {
                if (m_generation.load() == generation)
                    fire();
            });
        },
        Qt::QueuedConnection);
}

void QtOneShotTimer::cancel() {
    ++m_generation;
}
