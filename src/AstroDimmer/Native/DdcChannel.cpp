#include "pch.h"
#include "Native/DdcChannel.h"

namespace AstroDimmer::Native
{
    DdcChannel::DdcChannel()
    {
        m_thread = std::thread([this] { Run(); });
    }

    DdcChannel::~DdcChannel()
    {
        {
            std::scoped_lock guard{ m_lock };
            m_stop = true;
        }
        m_wake.notify_one();

        if (m_thread.joinable())
            m_thread.join();
    }

    void DdcChannel::Post(std::coroutine_handle<> handle)
    {
        {
            std::scoped_lock guard{ m_lock };
            m_queue.push_back(handle);
        }
        m_wake.notify_one();
    }

    void DdcChannel::Run()
    {
        SetThreadDescription(GetCurrentThread(), L"AstroDimmer DDC/CI");

        // DDC calls block on I2C; no benefit to competing with the UI.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        // WMI, for built-in panels, is COM. A multithreaded apartment needs
        // no message pump, which this thread does not have.
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        for (;;)
        {
            std::coroutine_handle<> next;
            {
                std::unique_lock guard{ m_lock };
                m_wake.wait(guard, [this] { return m_stop || !m_queue.empty(); });

                // At shutdown the queue is abandoned rather than run: those
                // coroutines would only hop back to a UI that is going away.
                if (m_stop) break;

                next = m_queue.front();
                m_queue.pop_front();
            }

            next.resume();
        }

        // Last thing on this thread, after all work.
        m_session.Close();
        m_backlight.Close();

        if (SUCCEEDED(com))
            CoUninitialize();
    }
}
