// client_util.h: small shared helpers for the media/stream network clients
// (plex, jellyfin, xcloud). Thread join, cache-dir mkdir, UUID generation.

#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include "host.h"

// Join a worker thread if it is running.
inline void JoinIf(std::thread& t) { if (t.joinable()) t.join(); }

// Create a directory. 0 when it exists afterwards.
inline int Mkdir_(const char* path) { return Host_MakeDir(path) ? 0 : -1; }

// Random RFC 4122 version-4 UUID as a 36-char string plus NUL.
inline void GenerateUUID(char out[37]) {
    unsigned char b[16];
    for (int i = 0; i < 16; i++) b[i] = (unsigned char)(rand() & 0xFF);
    b[6] = (b[6] & 0x0F) | 0x40;  // version 4
    b[8] = (b[8] & 0x3F) | 0x80;  // variant 1
    snprintf(out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

// One background thread running jobs in the order they were posted, so a
// stop report never overtakes the start it belongs to.
class SerialWorker {
public:
    ~SerialWorker() { Stop(); }
    void Post(std::function<void()> job) {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_quit) return;
        m_jobs.push_back(std::move(job));
        if (!m_thread.joinable()) m_thread = std::thread(&SerialWorker::Run, this);
        m_cv.notify_one();
    }
    // Drops what hasn't started and waits out the job that has.
    void Stop() {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_quit = true;
            m_jobs.clear();
            m_cv.notify_one();
        }
        JoinIf(m_thread);
    }
private:
    void Run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [this] { return m_quit || !m_jobs.empty(); });
                if (m_quit) return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            job();
        }
    }
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_jobs;
    std::thread m_thread;
    bool m_quit = false;
};
