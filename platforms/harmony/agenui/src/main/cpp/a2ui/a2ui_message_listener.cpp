#include "a2ui_message_listener.h"
#include "hilog/log.h"
#include "log/a2ui_capi_log.h"
#include "agenui_logger_internal.h"
#include "render/a2ui_component.h"
#include "render/a2ui_surface.h"
#include "render/factory/a2ui_default_registry.h"
#include <nlohmann/json.hpp>
#include "utils/a2ui_log_utils.h"
#include <cstdlib>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0000
#define LOG_TAG "A2UIMessageListener"

extern napi_env a2ui_get_napi_env();

namespace agenui {

// Static member initialization
std::map<int, A2UIMessageListener*>& A2UIMessageListener::getInstanceToListenerMap() {
    static auto* p = new std::map<int, A2UIMessageListener*>();
    return *p;
}
std::mutex A2UIMessageListener::s_instanceMapMutex_;

A2UIMessageListener::A2UIMessageListener(int instanceId)
    : instanceId_(instanceId), surfaceManager_(nullptr), tsfn_(nullptr) {
    surfaceManager_ = std::make_unique<a2ui::A2UISurfaceManager>(
        &a2ui::getDefaultFactoryRegistry(), instanceId_);

    // Register instance for exposure dispatch lookup
    {
        std::lock_guard<std::mutex> lock(s_instanceMapMutex_);
        getInstanceToListenerMap()[instanceId_] = this;
    }
    
    surfaceManager_->setBlankCheckExecutor([this](const std::string& surfaceId, uint64_t generation, int32_t minComponentCount) {
        std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
        postToMainThread([weakSelf, surfaceId, generation, minComponentCount](napi_env /*env*/) {
            auto self = weakSelf.lock();
            if (!self || !self->surfaceManager_) {
                return;
            }
            a2ui::A2UISurface* surface = self->surfaceManager_->getSurface(surfaceId);
            if (!surface) {
                return;
            }
            surface->performBlankCheckOnMainThread(generation, minComponentCount);
        });
    });
    surfaceManager_->setErrorReporter([this](const ErrorMessage& msg) {
        onError(msg);
    });
    surfaceManager_->setContentSizeChangedCallback([this](const std::string& surfaceId, float width, float height) {
        float widthVp = a2ui::UnitConverter::a2uiToVp(width);
        float heightVp = a2ui::UnitConverter::a2uiToVp(height);
        std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
        postToMainThread([weakSelf, surfaceId, widthVp, heightVp](napi_env env) {
            auto self = weakSelf.lock();
            if (!self) return;
            for (auto& ref : self->listeners_) {
                napi_value listener = nullptr;
                napi_get_reference_value(env, ref, &listener);
                if (!listener) continue;
                napi_value func = nullptr;
                napi_get_named_property(env, listener, "onContentSizeChanged", &func);
                napi_valuetype funcType = napi_undefined;
                if (func) napi_typeof(env, func, &funcType);
                if (funcType == napi_function) {
                    napi_value surfaceIdVal = nullptr;
                    napi_create_string_utf8(env, surfaceId.c_str(), NAPI_AUTO_LENGTH, &surfaceIdVal);
                    napi_value wVal = nullptr;
                    napi_create_double(env, static_cast<double>(widthVp), &wVal);
                    napi_value hVal = nullptr;
                    napi_create_double(env, static_cast<double>(heightVp), &hVal);
                    napi_value args[] = { surfaceIdVal, wVal, hVal };
                    napi_value result = nullptr;
                    napi_call_function(env, listener, func, 3, args, &result);
                }
            }
        });
    });

    surfaceManager_->setRootComponentUpdateCallback([this](const std::string& surfaceId, const std::string& propsJson) {
        dispatchRootComponentUpdate(surfaceId, propsJson);
    });

    HM_LOGI("A2UIMessageListener created, instanceId=%d", instanceId_);
}

A2UIMessageListener::~A2UIMessageListener() {
    // Unregister instance from exposure dispatch lookup
    {
        std::lock_guard<std::mutex> lock(s_instanceMapMutex_);
        getInstanceToListenerMap().erase(instanceId_);
    }

    // tsfn_ is owned by napi_init.cpp and must not be released here.
    tsfn_ = nullptr;

    napi_env env = a2ui_get_napi_env();
    for (auto& ref : listeners_) {
        napi_delete_reference(env, ref);
    }
    listeners_.clear();

    surfaceManager_.reset();

    HM_LOGI("A2UIMessageListener destroyed, instanceId=%d", instanceId_);
}

void A2UIMessageListener::setTsfn(napi_threadsafe_function tsfn) {
    tsfn_ = tsfn;
    HM_LOGI("setTsfn: tsfn assigned, instanceId=%d, tsfn=%p", instanceId_, (void*)tsfn);
}

void A2UIMessageListener::postToMainThread(MainThreadTask task) {
    if (!tsfn_) {
        HM_LOGE("postToMainThread: tsfn not initialized, instanceId=%d, dropping task", instanceId_);
        return;
    }
    auto* taskPtr = new MainThreadTask(std::move(task));
    napi_status status = napi_call_threadsafe_function(tsfn_, taskPtr, napi_tsfn_nonblocking);
    if (status != napi_ok) {
        HM_LOGE("postToMainThread: napi_call_threadsafe_function failed, instanceId=%d, status=%d", instanceId_, status);
        delete taskPtr;
    }
}

a2ui::A2UISurfaceManager* A2UIMessageListener::getSurfaceManager() const {
    return surfaceManager_.get();
}

A2UIMessageListener* A2UIMessageListener::findListenerByInstanceId(int instanceId) {
    if (instanceId == 0) return nullptr;
    std::lock_guard<std::mutex> lock(s_instanceMapMutex_);
    auto it = getInstanceToListenerMap().find(instanceId);
    return (it != getInstanceToListenerMap().end()) ? it->second : nullptr;
}

void A2UIMessageListener::dispatchComponentAppeared(const std::string& surfaceId,
                                                    const std::string& parentComponentId,
                                                    const std::string& parentType,
                                                    const std::string& properties) {
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId, parentComponentId, parentType, properties](napi_env env) {
        auto self = weakSelf.lock();
        if (!self) return;

        for (auto& ref : self->listeners_) {
            napi_value listener = nullptr;
            napi_get_reference_value(env, ref, &listener);
            if (!listener) continue;

            napi_value onComponentAppearedFunc = nullptr;
            napi_get_named_property(env, listener, "onComponentAppeared", &onComponentAppearedFunc);

            napi_valuetype funcType = napi_undefined;
            if (onComponentAppearedFunc) napi_typeof(env, onComponentAppearedFunc, &funcType);
            if (funcType != napi_function) continue;

            napi_value surfaceIdValue = nullptr;
            napi_create_string_utf8(env, surfaceId.c_str(), NAPI_AUTO_LENGTH, &surfaceIdValue);
            napi_value parentComponentIdValue = nullptr;
            napi_create_string_utf8(env, parentComponentId.c_str(), NAPI_AUTO_LENGTH, &parentComponentIdValue);
            napi_value parentTypeValue = nullptr;
            napi_create_string_utf8(env, parentType.c_str(), NAPI_AUTO_LENGTH, &parentTypeValue);
            napi_value propertiesValue = nullptr;
            napi_create_string_utf8(env, properties.c_str(), NAPI_AUTO_LENGTH, &propertiesValue);

            napi_value args[] = { surfaceIdValue, parentComponentIdValue, parentTypeValue, propertiesValue };
            napi_value result = nullptr;
            napi_call_function(env, listener, onComponentAppearedFunc, 4, args, &result);
        }
    });
}

void A2UIMessageListener::dispatchRootComponentUpdate(const std::string& surfaceId,
                                                      const std::string& propsJson) {
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId, propsJson](napi_env env) {
        auto self = weakSelf.lock();
        if (!self) return;

        for (auto& ref : self->listeners_) {
            napi_value listener = nullptr;
            napi_get_reference_value(env, ref, &listener);
            if (!listener) continue;

            napi_value onRootComponentUpdateFunc = nullptr;
            napi_get_named_property(env, listener, "onRootComponentUpdate", &onRootComponentUpdateFunc);

            napi_valuetype funcType = napi_undefined;
            if (onRootComponentUpdateFunc) napi_typeof(env, onRootComponentUpdateFunc, &funcType);
            if (funcType != napi_function) continue;

            napi_value surfaceIdValue = nullptr;
            napi_create_string_utf8(env, surfaceId.c_str(), NAPI_AUTO_LENGTH, &surfaceIdValue);
            napi_value propsValue = nullptr;
            napi_create_string_utf8(env, propsJson.c_str(), NAPI_AUTO_LENGTH, &propsValue);

            napi_value args[] = { surfaceIdValue, propsValue };
            napi_value result = nullptr;
            napi_call_function(env, listener, onRootComponentUpdateFunc, 2, args, &result);
        }
    });
}

// ==================== IAGenUIMessageListener Implementation ====================

void A2UIMessageListener::onCreateSurface(const CreateSurfaceMessage& msg) {
    HM_LOGI("instanceId=%d, surfaceId=%s, catalogId=%s" , instanceId_, msg.surfaceId.c_str(), msg.catalogId.c_str());
    // listeners_ and surfaceManager_ are only touched on the main thread.
    std::string surfaceId = msg.surfaceId;
//    bool animated = msg.animated;
    // 默认禁用动画，避免触发动画 crash case
    bool animated = false;
    std::string rawProtocolContent = msg.rawProtocolContent;
    // Capture weak_ptr instead of `this`: the main-thread task may run after
    // the listener has been destroyed (e.g. rapid create/destroy on the main
    // thread keeps the TSFN queue from draining).
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId, animated, rawProtocolContent](napi_env env) {
        auto self = weakSelf.lock();
        if (!self || !self->surfaceManager_) {
            HM_LOGW("onCreateSurface: listener already destroyed, surfaceId=%s", surfaceId.c_str());
            return;
        }
        a2ui::A2UISurface* surface = self->surfaceManager_->createSurface(surfaceId, animated);
        if (!surface) {
            HM_LOGE("Failed to create surface: %s", surfaceId.c_str());
            return;
        }

        // Blank-screen detection is now managed by the host layer via
        // Surface.startBlankCheck() in the onCreateSurface listener callback.

        // Notify ArkTS listeners after the surface is created.
        for (auto& ref : self->listeners_) {
            napi_value listener = nullptr;
            napi_get_reference_value(env, ref, &listener);
            if (!listener) continue;

            napi_value onCreatedFunc = nullptr;
            napi_get_named_property(env, listener, "onCreateSurface", &onCreatedFunc);

            napi_valuetype funcType = napi_undefined;
            if (onCreatedFunc) napi_typeof(env, onCreatedFunc, &funcType);
            if (funcType == napi_function) {
                napi_value surfaceIdValue = nullptr;
                napi_create_string_utf8(env, surfaceId.c_str(), NAPI_AUTO_LENGTH, &surfaceIdValue);
                napi_value rawProtocolContentValue = nullptr;
                napi_create_string_utf8(env, rawProtocolContent.c_str(), NAPI_AUTO_LENGTH, &rawProtocolContentValue);
                napi_value args[] = { surfaceIdValue, rawProtocolContentValue };
                napi_value result = nullptr;
                napi_call_function(env, listener, onCreatedFunc, 2, args, &result);  // 2 args: surfaceId, rawProtocolContent
            }
        }
    });
}

void A2UIMessageListener::onContentHandleReady() {
    HM_LOGI("Method deprecated, use bindSurface instead");
}

void A2UIMessageListener::onDeleteSurface(const DeleteSurfaceMessage& msg) {
    HM_LOGI("instanceId=%d, surfaceId=%s", instanceId_, msg.surfaceId.c_str());

    // listeners_ and surfaceManager_ are only touched on the main thread.
    std::string surfaceId = msg.surfaceId;
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId](napi_env env) {
        auto self = weakSelf.lock();
        if (!self || !self->surfaceManager_) {
            // Surface was already torn down with the listener.
            return;
        }
        a2ui::A2UISurface* surface = self->surfaceManager_->getSurface(surfaceId);
        if (surface) {
            HM_LOGI("onDeleteSurface: cancel blank check, surfaceId=%s", surfaceId.c_str());
            surface->cancelBlankCheck();
        } else {
            HM_LOGI("onDeleteSurface: surface missing before cancel, surfaceId=%s", surfaceId.c_str());
        }
        for (auto& ref : self->listeners_) {
            napi_value listener = nullptr;
            napi_get_reference_value(env, ref, &listener);
            if (!listener) continue;

            napi_value onDestroyedFunc = nullptr;
            napi_get_named_property(env, listener, "onDeleteSurface", &onDestroyedFunc);

            napi_valuetype funcType = napi_undefined;
            if (onDestroyedFunc) napi_typeof(env, onDestroyedFunc, &funcType);
            if (funcType == napi_function) {
                napi_value surfaceIdValue = nullptr;
                napi_create_string_utf8(env, surfaceId.c_str(), NAPI_AUTO_LENGTH, &surfaceIdValue);
                napi_value result = nullptr;
                napi_call_function(env, listener, onDestroyedFunc, 1, &surfaceIdValue, &result);
            }
        }

        self->surfaceManager_->destroySurface(surfaceId);

        HM_LOGI("Surface destroyed: %s, remaining: %d", surfaceId.c_str(), self->surfaceManager_->getSurfaceCount());
    });
}

void A2UIMessageListener::onComponentsUpdate(const std::string &surfaceId, const std::vector<ComponentsUpdateMessage> &msg) {
    for (size_t msgIndex = 0; msgIndex < msg.size(); ++msgIndex) {
        std::string brief = A2UILogUtils::formatComponentBrief(msg[msgIndex].component);
        HM_LOGI("->msg%zu: %s", msgIndex + 1, brief.c_str());
    }

    // surfaceManager_ is only accessed on the main thread.
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId, msg](napi_env /*env*/) {
        auto self = weakSelf.lock();
        if (!self || !self->surfaceManager_) return;  // listener torn down before task ran
        a2ui::A2UISurface* surface = self->surfaceManager_->getSurface(surfaceId);
        if (!surface) {
            HM_LOGE("onComponentsUpdate: Surface not found: %s", surfaceId.c_str());
            return;
        }
        surface->handleComponentsUpdate(msg);
        AGENUI_PERFORMANCE_LOG("components_applied", "%s", surfaceId.c_str());
    });
}

void A2UIMessageListener::onComponentsAdd(const std::string &surfaceId, const std::vector<ComponentsAddMessage> &msg) {
    for (size_t msgIndex = 0; msgIndex < msg.size(); ++msgIndex) {
        std::string brief = A2UILogUtils::formatComponentBrief(msg[msgIndex].component);
        HM_LOGI("->msg%zu: %s", msgIndex + 1, brief.c_str());
    }

    // surfaceManager_ is only accessed on the main thread.
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId, msg](napi_env /*env*/) {
        auto self = weakSelf.lock();
        if (!self || !self->surfaceManager_) return;
        a2ui::A2UISurface* surface = self->surfaceManager_->getSurface(surfaceId);
        if (!surface) {
            HM_LOGE("onComponentsAdd: Surface not found: %s", surfaceId.c_str());
            return;
        }
        for (const auto& m : msg) {
            surface->handleComponentAdd(m);
        }
        AGENUI_PERFORMANCE_LOG("components_applied", "%s", surfaceId.c_str());
    });
}

void A2UIMessageListener::onComponentsRemove(const std::string &surfaceId, const std::vector<ComponentsRemoveMessage> &msg) {
    HM_LOGI("surfaceId: %s, count: %zu", surfaceId.c_str(), msg.size());

    // surfaceManager_ is only accessed on the main thread.
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, surfaceId, msg](napi_env /*env*/) {
        auto self = weakSelf.lock();
        if (!self || !self->surfaceManager_) return;
        a2ui::A2UISurface* surface = self->surfaceManager_->getSurface(surfaceId);
        if (!surface) {
            HM_LOGE("onComponentsRemove: Surface not found: %s", surfaceId.c_str());
            return;
        }
        surface->handleComponentsRemove(msg);
    });
}

void A2UIMessageListener::onActionEventRouted(const std::string &content) {
    HM_LOGI("instanceId=%d, content: %s", instanceId_, content.c_str());

    // listeners_ are only accessed on the main thread.
    std::string contentCopy = content;
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, contentCopy](napi_env env) {
        auto self = weakSelf.lock();
        if (!self) return;
        for (auto& ref : self->listeners_) {
            napi_value listener = nullptr;
            napi_get_reference_value(env, ref, &listener);
            if (!listener) continue;

            napi_value onActionEventRoutedFunc = nullptr;
            napi_get_named_property(env, listener, "onActionEventRouted", &onActionEventRoutedFunc);

            napi_valuetype funcType = napi_undefined;
            if (onActionEventRoutedFunc) napi_typeof(env, onActionEventRoutedFunc, &funcType);
            if (funcType == napi_function) {
                napi_value contentValue = nullptr;
                napi_create_string_utf8(env, contentCopy.c_str(), NAPI_AUTO_LENGTH, &contentValue);
                napi_value result = nullptr;
                napi_call_function(env, listener, onActionEventRoutedFunc, 1, &contentValue, &result);
            }
        }
    });
}

void A2UIMessageListener::onError(const ErrorMessage& msg) {
    HM_LOGE("instanceId=%d, code=%d, surfaceId=%s, message=%s",
            instanceId_, msg.code, msg.surfaceId.c_str(), msg.message.c_str());

    int32_t code = msg.code;
    std::string surfaceId = msg.surfaceId;
    std::string message = msg.message;
    std::weak_ptr<A2UIMessageListener> weakSelf = weak_from_this();
    postToMainThread([weakSelf, code, surfaceId, message](napi_env env) {
        auto self = weakSelf.lock();
        if (!self) return;
        for (auto& ref : self->listeners_) {
            napi_value listener = nullptr;
            napi_get_reference_value(env, ref, &listener);
            if (!listener) continue;

            napi_value onErrorFunc = nullptr;
            napi_get_named_property(env, listener, "onError", &onErrorFunc);

            napi_valuetype funcType = napi_undefined;
            if (onErrorFunc) napi_typeof(env, onErrorFunc, &funcType);
            if (funcType == napi_function) {
                napi_value codeValue = nullptr;
                napi_create_int32(env, code, &codeValue);
                napi_value surfaceIdValue = nullptr;
                napi_create_string_utf8(env, surfaceId.c_str(), NAPI_AUTO_LENGTH, &surfaceIdValue);
                napi_value messageValue = nullptr;
                napi_create_string_utf8(env, message.c_str(), NAPI_AUTO_LENGTH, &messageValue);
                napi_value args[] = { codeValue, surfaceIdValue, messageValue };
                napi_value result = nullptr;
                napi_call_function(env, listener, onErrorFunc, 3, args, &result);
            }
        }
    });
}

void A2UIMessageListener::registerListener(napi_value listener) {
    napi_env env = a2ui_get_napi_env();
    
    napi_ref ref = nullptr;
    napi_create_reference(env, listener, 1, &ref);
    listeners_.push_back(ref);
    
    HM_LOGI("instanceId=%d, listener registered, total: %zu", instanceId_, listeners_.size());
}

void A2UIMessageListener::unregisterListener(napi_value listener) {
    napi_env env = a2ui_get_napi_env();
    
    if (listeners_.empty()) {
        HM_LOGW("instanceId=%d, no listeners registered", instanceId_);
        return;
    }
    
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
        napi_value existingListener = nullptr;
        napi_get_reference_value(env, *it, &existingListener);
        
        bool isEqual = false;
        napi_strict_equals(env, listener, existingListener, &isEqual);
        
        if (isEqual) {
            napi_delete_reference(env, *it);
            listeners_.erase(it);
            HM_LOGI("instanceId=%d, listener unregistered, remaining: %zu", instanceId_, listeners_.size());
            return;
        }
    }
    
    HM_LOGW("instanceId=%d, listener not found", instanceId_);
}

} // namespace agenui
