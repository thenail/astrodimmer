#pragma once

#include <coroutine>
#include <deque>
#include "Native/DdcSession.h"

namespace AstroDimmer::Native
{
    /// The single thread through which every DDC/CI call is made.
    ///
    /// Hard rule: one thread. DDC/CI runs over I2C: it is slow, and monitors
    /// tolerate concurrent transactions poorly. Nothing serialises that for
    /// free, so it is explicit here.
    ///
    /// A coroutine hops onto the thread with `co_await ddc.Enter()`, talks to
    /// Session() for as long as it needs, and hops back to the UI with
    /// wil::resume_foreground. Coroutines queue in arrival order and each one
    /// holds the thread until it leaves, so two conversations with a monitor
    /// can never interleave. Do not "optimise" enumeration by parallelising
    /// across monitors - it is the likeliest way to break displays that work.
    class DdcChannel
    {
    public:
        DdcChannel();
        ~DdcChannel();

        DdcChannel(DdcChannel const&) = delete;
        DdcChannel& operator=(DdcChannel const&) = delete;

        struct EnterAwaiter
        {
            DdcChannel* Channel;

            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle) { Channel->Post(handle); }
            void await_resume() const noexcept {}
        };

        /// Resumes the awaiting coroutine on the DDC thread.
        EnterAwaiter Enter() { return { this }; }

        /// Only to be touched on the DDC thread.
        DdcSession& Session() { return m_session; }

    private:
        void Post(std::coroutine_handle<> handle);
        void Run();

        std::mutex m_lock;
        std::condition_variable m_wake;
        std::deque<std::coroutine_handle<>> m_queue;
        bool m_stop{ false };
        DdcSession m_session;
        std::thread m_thread;
    };
}
