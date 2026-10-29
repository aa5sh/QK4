// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/rtsemaphore.h"

#if defined(Q_OS_DARWIN)
#include <dispatch/dispatch.h>
#elif defined(Q_OS_WIN)
#include <climits>
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#endif

#if defined(Q_OS_DARWIN)

RtSemaphore::RtSemaphore() : m_sem(dispatch_semaphore_create(0)) {}

RtSemaphore::~RtSemaphore() {
    dispatch_release(static_cast<dispatch_semaphore_t>(m_sem));
}

void RtSemaphore::post() {
    dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(m_sem));
}

void RtSemaphore::wait() {
    dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(m_sem), DISPATCH_TIME_FOREVER);
}

#elif defined(Q_OS_WIN)

RtSemaphore::RtSemaphore() : m_sem(CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr)) {}

RtSemaphore::~RtSemaphore() {
    if (m_sem)
        CloseHandle(static_cast<HANDLE>(m_sem));
}

void RtSemaphore::post() {
    ReleaseSemaphore(static_cast<HANDLE>(m_sem), 1, nullptr);
}

void RtSemaphore::wait() {
    WaitForSingleObject(static_cast<HANDLE>(m_sem), INFINITE);
}

#elif defined(Q_OS_LINUX)

RtSemaphore::RtSemaphore() {
    sem_init(&m_sem, 0, 0);
}

RtSemaphore::~RtSemaphore() {
    sem_destroy(&m_sem);
}

void RtSemaphore::post() {
    sem_post(&m_sem);
}

void RtSemaphore::wait() {
    while (sem_wait(&m_sem) != 0 && errno == EINTR) {
    }
}

#else
#error "RtSemaphore: no implementation for this platform"
#endif
