// Configurable IPlatformFunction implementation for tests.

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "agenui_platform_function.h"

namespace agenui {
namespace testing {

class MockPlatformFunction : public ::agenui::IPlatformFunction {
public:
    struct CallRecord {
        std::string params;
    };

    using CallSyncHandler = std::function<::agenui::FunctionCallResult(const std::string&)>;

    ::agenui::FunctionCallResult callSync(const std::string& params) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            calls_.push_back({params});
        }
        ++callCount_;
        if (handler_) {
            return handler_(params);
        }
        ::agenui::FunctionCallResult res;
        res.status = ::agenui::FunctionCallStatus::Success;
        res.data = "{}";
        return res;
    }
    
    ::agenui::FunctionCallResult callAsync(const std::string& params,
                                           const FunctionCallCallback& callback) override {
        return ::agenui::FunctionCallResult();
    }

    void setHandler(CallSyncHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handler_ = std::move(handler);
    }

    std::vector<CallRecord> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }

    int callCount() const { return callCount_.load(); }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        calls_.clear();
        callCount_.store(0);
    }

private:
    mutable std::mutex mutex_;
    std::vector<CallRecord> calls_;
    CallSyncHandler handler_;
    std::atomic<int> callCount_{0};
};

}  // namespace testing
}  // namespace agenui
