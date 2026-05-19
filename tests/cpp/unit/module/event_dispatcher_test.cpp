// ED*: EventDispatcher unit tests (white-box).

#include <gtest/gtest.h>

#include "agenui_dispatcher_types.h"
#include "agenui_message_listener.h"
#include "module/agenui_event_dispatcher.h"
#include "support/mock_message_listener.h"

namespace {

// ED001
TEST(EventDispatcherTest, ED001_AddNullListener_Ignored) {
    ::agenui::EventDispatcher d;
    EXPECT_NO_THROW(d.addEventListener(nullptr));
}

// ED002
TEST(EventDispatcherTest, ED002_RemoveNullListener_Ignored) {
    ::agenui::EventDispatcher d;
    EXPECT_NO_THROW(d.removeEventListener(nullptr));
}

// ED003
TEST(EventDispatcherTest, ED003_DispatchCreateSurface_AllListenersInvoked) {
    ::agenui::EventDispatcher d;
    ::agenui::testing::MockMessageListener a, b;
    d.addEventListener(&a);
    d.addEventListener(&b);

    ::agenui::CreateSurfaceMessage msg;
    msg.surfaceId = "ED003";
    d.dispatchCreateSurface(msg);

    EXPECT_EQ(a.createSurfaceCalls.size(), 1u);
    EXPECT_EQ(b.createSurfaceCalls.size(), 1u);
    d.removeAllEventListeners();
}


}  // namespace
