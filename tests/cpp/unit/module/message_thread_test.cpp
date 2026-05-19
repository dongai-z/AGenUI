// MT*: MessageThread unit tests.

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "module/agenui_message_thread.h"

namespace {

// MT001
TEST(MessageThreadTest, MT001_PostBeforeStart_DroppedSafely) {
    ::agenui::MessageThread t("MT001");
    std::atomic<int> count{0};
    t.post([&]() { ++count; });
    // Without start(), task should not run.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count.load(), 0);
    t.stop();
}

// MT002
TEST(MessageThreadTest, MT002_PostTask_Executed) {
    ::agenui::MessageThread t("MT002");
    ASSERT_TRUE(t.start());
    std::atomic<int> count{0};
    t.post([&]() { ++count; });
    for (int i = 0; i < 100 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GT(count.load(), 0);
    t.stop();
}

// MT003
TEST(MessageThreadTest, MT003_PostDelayed_DoesNotRunBeforeDelay) {
    ::agenui::MessageThread t("MT003");
    ASSERT_TRUE(t.start());
    std::atomic<int> count{0};
    auto begin = std::chrono::steady_clock::now();
    t.postDelayed([&]() {
        count.store(1);
    }, 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count.load(), 0);
    for (int i = 0; i < 200 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - begin)
            .count();
    EXPECT_EQ(count.load(), 1);
    EXPECT_GE(elapsed, 150);
    t.stop();
}

// MT004
TEST(MessageThreadTest, MT004_StopDrainsQueue_NoLeak) {
    ::agenui::MessageThread t("MT004");
    ASSERT_TRUE(t.start());
    for (int i = 0; i < 200; ++i) {
        t.post([]() {});
    }
    t.stop();  // should not hang
    SUCCEED();
}

// MT005
TEST(MessageThreadTest, MT005_PostFromWorkerThread_OK) {
    ::agenui::MessageThread t("MT005");
    ASSERT_TRUE(t.start());
    std::atomic<int> count{0};
    t.post([&]() {
        t.post([&]() { ++count; });
    });
    for (int i = 0; i < 100 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GT(count.load(), 0);
    t.stop();
}

// MT006
TEST(MessageThreadTest, MT006_FIFO_Order) {
    ::agenui::MessageThread t("MT006");
    ASSERT_TRUE(t.start());
    std::vector<int> sequence;
    std::mutex seqMutex;
    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        t.post([i, &sequence, &seqMutex]() {
            std::lock_guard<std::mutex> lk(seqMutex);
            sequence.push_back(i);
        });
    }
    // Wait until done. Crucially, do NOT hold the mutex while sleeping —
    // the worker thread also needs it to push_back.
    for (int i = 0; i < 200; ++i) {
        size_t s;
        {
            std::lock_guard<std::mutex> lk(seqMutex);
            s = sequence.size();
        }
        if (s == static_cast<size_t>(N)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    {
        std::lock_guard<std::mutex> lk(seqMutex);
        ASSERT_EQ(sequence.size(), static_cast<size_t>(N));
        for (int i = 0; i < N; ++i) EXPECT_EQ(sequence[i], i);
    }
    t.stop();
}

// MT007
TEST(MessageThreadTest, MT007_GetThreadId_StableAfterStart) {
    ::agenui::MessageThread t("MT007");
    ASSERT_TRUE(t.start());
    auto id1 = t.getThreadId();
    auto id2 = t.getThreadId();
    EXPECT_EQ(id1, id2);
    t.stop();
}

// MT008
TEST(MessageThreadTest, MT008_StartAfterStop_Restarts) {
    ::agenui::MessageThread t("MT008");
    ASSERT_TRUE(t.start());
    t.stop();
    EXPECT_TRUE(t.start());
    std::atomic<int> count{0};
    t.post([&]() { ++count; });
    for (int i = 0; i < 100 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GT(count.load(), 0);
    t.stop();
}

}  // namespace
