// TM*: ThreadManager unit tests.

#include <gtest/gtest.h>

#include "module/agenui_thread_manager.h"

namespace {

// Note: ThreadManager is a process-wide singleton owned by AGenUIEngine
// (created at engine start, destroyed at engine stop). We use a custom
// thread id so we don't conflict with AGENUI_SHARED_THREAD_ID (= 1).
constexpr int kTestThreadId = 9991;
constexpr int kTestThreadId2 = 9992;

// TM001
TEST(ThreadManagerTest, TM001_CreateThread_Once) {
    auto& m = ::agenui::ThreadManager::getInstance();
    EXPECT_TRUE(m.createThread(kTestThreadId));
    EXPECT_NE(m.getMessageThread(kTestThreadId), nullptr);
    m.destroyThread(kTestThreadId);
}

// TM002
TEST(ThreadManagerTest, TM002_CreateThread_Idempotent) {
    auto& m = ::agenui::ThreadManager::getInstance();
    EXPECT_TRUE(m.createThread(kTestThreadId2));
    EXPECT_TRUE(m.createThread(kTestThreadId2));  // duplicate is no-op
    m.destroyThread(kTestThreadId2);
}

// TM003
TEST(ThreadManagerTest, TM003_DestroyThread_Removes) {
    auto& m = ::agenui::ThreadManager::getInstance();
    m.createThread(kTestThreadId);
    m.destroyThread(kTestThreadId);
    EXPECT_EQ(m.getMessageThread(kTestThreadId), nullptr);
}

// TM004
TEST(ThreadManagerTest, TM004_GetMessageThread_NonExistent_ReturnsNull) {
    auto& m = ::agenui::ThreadManager::getInstance();
    EXPECT_EQ(m.getMessageThread(424242), nullptr);
}

}  // namespace
