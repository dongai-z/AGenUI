// Helpers for synchronizing with the engine's worker thread.
//
// AGenUI dispatches almost every external API to the shared worker thread
// via `messageThread->post(lambda)`. Tests need a way to "wait until all
// previously posted lambdas have executed". The trick: post our own lambda
// that signals a promise, then wait on the future on the main thread.

#pragma once

#include <chrono>
#include <future>
#include <thread>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_surface_manager_interface.h"
#include "module/agenui_message_thread.h"
#include "module/agenui_thread_manager.h"
#include "agenui_type_define.h"

namespace agenui {
namespace testing {

// Wait until the shared worker thread has drained all previously posted
// tasks. Returns true on success, false on timeout.
//
// Note: relies on FIFO ordering of MessageThread's task queue.
inline bool WaitForWorkerIdle(int timeoutMillis = 2000) {
    auto* thread = ::agenui::ThreadManager::getInstance()
                       .getMessageThread(AGENUI_SHARED_THREAD_ID);
    if (!thread) {
        return true;  // No worker thread => trivially idle.
    }

    auto promise = std::make_shared<std::promise<void>>();
    std::future<void> future = promise->get_future();
    thread->post([promise]() { promise->set_value(); });

    return future.wait_for(std::chrono::milliseconds(timeoutMillis)) ==
           std::future_status::ready;
}

// Spin-wait helper: poll the predicate until it returns true or timeout.
// Useful when we need to observe an asynchronous side effect that may
// require multiple worker-thread hops.
template <typename Pred>
inline bool WaitFor(Pred predicate, int timeoutMillis = 2000,
                    int pollIntervalMillis = 5) {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(timeoutMillis);
    while (clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(pollIntervalMillis));
    }
    return predicate();
}

}  // namespace testing
}  // namespace agenui
