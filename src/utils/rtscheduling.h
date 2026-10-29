// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scheduling class for QK4's timing-critical threads. A thread calls promoteCurrentThread() on
// itself, first thing, and gets the best the OS will grant, falling back quietly when it refuses:
//
//   class     | macOS                               | Windows                         | Linux / Pi
//   ----------|-------------------------------------|---------------------------------|---------------------------
//   RealTime  | Mach time-constraint policy, else   | MMCSS "Pro Audio" (high), else  | SCHED_FIFO 15 with
//             | QoS user-interactive                | TIME_CRITICAL, else HIGHEST     | SCHED_RESET_ON_FORK, else
//             |                                     |                                 | nice -10, else unchanged
//   High      | QoS user-interactive                | THREAD_PRIORITY_HIGHEST         | nice -10, else unchanged
//   Normal    | unchanged                           | unchanged                       | unchanged
//
// Linux also cuts the thread's timer slack to 1 ns for RealTime and High, whatever else it gets.
//
// The decision for each OS is a pure function over that OS's calls (applyMac, applyWindows,
// applyLinux), so every platform's fallback chain is unit-tested on every platform. A real-time
// thread may not log (CONVENTIONS Rule 16), so promoteCurrentThread() records its result in a
// fixed lock-free table and an ordinary thread reports it later with takeUnreported().

#ifndef RTSCHEDULING_H
#define RTSCHEDULING_H

#include <vector>

namespace RtScheduling {

enum class Class { RealTime, High, Normal };
enum class Outcome { RealTime, Elevated, Unchanged };

struct Result {
    Class requested = Class::Normal;
    Outcome outcome = Outcome::Unchanged;
    const char *mechanism = "default"; // a static string: what the thread runs under now
    int error = 0;                     // the OS error of the first refused step, 0 if none
};

// Promotes the calling thread and records the result under `threadName` (a string literal).
// Never allocates, locks or logs, so it is safe on the thread it promotes.
Result promoteCurrentThread(Class requested, const char *threadName);

struct Report {
    const char *thread;
    Result result;
};

// Every result recorded since the last call, oldest first. For an ordinary thread to log.
std::vector<Report> takeUnreported();

const char *className(Class c);
const char *outcomeName(Outcome o);

// ---- the per-OS decisions, behind injectable calls ----

// The Linux SCHED_FIFO priority: well below PipeWire's and JACK's data threads (rt.prio 88 by
// default), so QK4's threads never delay the audio server they feed.
constexpr int kLinuxFifoPriority = 15;
constexpr int kLinuxHighNice = -10;

struct MacOps {
    void *context = nullptr;
    int (*setQosUserInteractive)(void *context) = nullptr; // 0 or an errno
    int (*setTimeConstraint)(void *context) = nullptr;     // 0 or a kern_return_t
};
Result applyMac(Class requested, const MacOps &ops);

enum class WindowsPriority { Highest, TimeCritical };
struct WindowsOps {
    void *context = nullptr;
    int (*joinMmcssProAudio)(void *context) = nullptr;                    // 0 or GetLastError()
    int (*setThreadPriority)(void *context, WindowsPriority p) = nullptr; // 0 or GetLastError()
};
Result applyWindows(Class requested, const WindowsOps &ops);

struct LinuxOps {
    void *context = nullptr;
    int (*setFifo)(void *context, int priority) = nullptr; // 0 or an errno
    int (*setNice)(void *context, int nice) = nullptr;     // 0 or an errno
    int (*setTimerSlackNs)(void *context, unsigned long ns) = nullptr;
};
Result applyLinux(Class requested, const LinuxOps &ops);

} // namespace RtScheduling

#endif // RTSCHEDULING_H
