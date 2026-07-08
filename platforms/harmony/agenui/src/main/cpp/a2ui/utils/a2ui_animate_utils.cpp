#include "a2ui_animate_utils.h"

#include <arkui/native_interface.h>
#include <arkui/native_animate.h>
#include "a2ui/render/a2ui_node.h"
#include "log/a2ui_capi_log.h"

namespace a2ui {

namespace {

float clampOpacity(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

/// Wrapper used by animateNodeOpacityAfterMount to pass the outPayload pointer
/// through the C-style post-frame callback.
struct AppearPostFrameData {
    ArkUI_NodeHandle     nodeHandle;
    float                targetOpacity;
    int32_t              durationMs;
    OpacityAnimatePayload** outPayload;
};

void onAppearAnimatePostFrame(uint64_t /*nanoTimestamp*/, uint32_t /*frameCount*/, void* userData) {
    auto* data = static_cast<AppearPostFrameData*>(userData);
    if (data == nullptr) {
        return;
    }
    animateNodeOpacityNow(data->nodeHandle, data->targetOpacity, data->durationMs, data->outPayload);
    delete data;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ArkUI_NativeAnimateAPI_1* getAnimateApi() {
    static ArkUI_NativeAnimateAPI_1* animateApi = [] {
        ArkUI_NativeAnimateAPI_1* api = nullptr;
        OH_ArkUI_GetModuleInterface(ARKUI_NATIVE_ANIMATE, ArkUI_NativeAnimateAPI_1, api);
        if (api == nullptr) {
            HM_LOGE("Fatal: Failed to get ArkUI NativeAnimateAPI_1");
        }
        return api;
    }();
    return animateApi;
}

void animateNodeOpacityNow(ArkUI_NodeHandle nodeHandle, float targetOpacity, int32_t durationMs,
                           OpacityAnimatePayload** outPayload) {
    if (nodeHandle == nullptr) {
        return;
    }

    targetOpacity = clampOpacity(targetOpacity);
    if (durationMs <= 0) {
        A2UINode(nodeHandle).setOpacity(targetOpacity);
        return;
    }

    ArkUI_ContextHandle context = OH_ArkUI_GetContextByNode(nodeHandle);
    ArkUI_NativeAnimateAPI_1* animateApi = getAnimateApi();
    if (context == nullptr || animateApi == nullptr) {
        A2UINode(nodeHandle).setOpacity(targetOpacity);
        return;
    }

    ArkUI_AnimatorOption* option = OH_ArkUI_AnimatorOption_Create(0);
    if (option == nullptr) {
        A2UINode(nodeHandle).setOpacity(targetOpacity);
        return;
    }

    auto* payload = new OpacityAnimatePayload();
    payload->nodeHandle     = nodeHandle;
    payload->targetOpacity  = targetOpacity;
    payload->durationMs     = durationMs;
    payload->backPtr        = outPayload;

    ArkUI_CurveHandle curve = OH_ArkUI_Curve_CreateCubicBezierCurve(0.42f, 0.0f, 0.58f, 1.0f);
    OH_ArkUI_AnimatorOption_SetDuration(option, durationMs);
    OH_ArkUI_AnimatorOption_SetBegin(option, 0.0f);
    OH_ArkUI_AnimatorOption_SetEnd(option, targetOpacity);
    OH_ArkUI_AnimatorOption_SetIterations(option, 1);
    OH_ArkUI_AnimatorOption_SetFill(option, ARKUI_ANIMATION_FILL_MODE_FORWARDS);
    OH_ArkUI_AnimatorOption_SetDirection(option, ARKUI_ANIMATION_DIRECTION_NORMAL);
    if (curve != nullptr) {
        OH_ArkUI_AnimatorOption_SetCurve(option, curve);
    }

    OH_ArkUI_AnimatorOption_RegisterOnFrameCallback(
        option,
        payload,
        [](ArkUI_AnimatorOnFrameEvent* event) {
            auto* p = static_cast<OpacityAnimatePayload*>(
                OH_ArkUI_AnimatorOnFrameEvent_GetUserData(event));
            if (p == nullptr || p->destroyed || p->nodeHandle == nullptr) {
                return;
            }
            A2UINode(p->nodeHandle).setOpacity(OH_ArkUI_AnimatorOnFrameEvent_GetValue(event));
        });

    auto finish = [](ArkUI_AnimatorEvent* event) {
        auto* p = static_cast<OpacityAnimatePayload*>(OH_ArkUI_AnimatorEvent_GetUserData(event));
        if (p == nullptr) {
            return;
        }
        if (!p->destroyed && p->nodeHandle != nullptr) {
            A2UINode(p->nodeHandle).setOpacity(p->targetOpacity);
        }
        ArkUI_NativeAnimateAPI_1* api = getAnimateApi();
        if (api != nullptr && p->animatorHandle != nullptr) {
            api->disposeAnimator(p->animatorHandle);
        }
        // Null the caller's tracking pointer before deleting, so the caller
        // never holds a dangling pointer after natural completion.
        if (p->backPtr != nullptr) {
            *(p->backPtr) = nullptr;
        }
        delete p;
    };

    OH_ArkUI_AnimatorOption_RegisterOnFinishCallback(option, payload, finish);
    OH_ArkUI_AnimatorOption_RegisterOnCancelCallback(option, payload, finish);

    ArkUI_AnimatorHandle animatorHandle = animateApi->createAnimator(context, option);
    payload->animatorHandle = animatorHandle;
    if (animatorHandle == nullptr) {
        A2UINode(nodeHandle).setOpacity(targetOpacity);
        delete payload;
        if (outPayload) *outPayload = nullptr;
    } else if (OH_ArkUI_Animator_Play(animatorHandle) != ARKUI_ERROR_CODE_NO_ERROR) {
        animateApi->disposeAnimator(animatorHandle);
        A2UINode(nodeHandle).setOpacity(targetOpacity);
        delete payload;
        if (outPayload) *outPayload = nullptr;
    } else {
        if (outPayload) *outPayload = payload;
    }

    if (curve != nullptr) {
        OH_ArkUI_Curve_DisposeCurve(curve);
    }
    OH_ArkUI_AnimatorOption_Dispose(option);
}

void animateNodeOpacityAfterMount(ArkUI_NodeHandle nodeHandle, float targetOpacity, int32_t durationMs,
                                  OpacityAnimatePayload** outPayload) {
    if (nodeHandle == nullptr) {
        return;
    }

    ArkUI_ContextHandle context = OH_ArkUI_GetContextByNode(nodeHandle);
    if (context == nullptr) {
        animateNodeOpacityNow(nodeHandle, targetOpacity, durationMs, outPayload);
        return;
    }

    auto* data = new AppearPostFrameData();
    data->nodeHandle    = nodeHandle;
    data->targetOpacity = clampOpacity(targetOpacity);
    data->durationMs    = durationMs;
    data->outPayload    = outPayload;
    if (postFrameCallbackCompat(context, data, onAppearAnimatePostFrame) != ARKUI_ERROR_CODE_NO_ERROR) {
        delete data;
        animateNodeOpacityNow(nodeHandle, targetOpacity, durationMs, outPayload);
    }
}

void cancelOpacityAnimator(OpacityAnimatePayload*& payload) {
    if (payload == nullptr) {
        return;
    }
    // Mark as destroyed so onFrame callbacks bail out immediately.
    payload->destroyed  = true;
    payload->nodeHandle = nullptr;
    ArkUI_AnimatorHandle handle = payload->animatorHandle;
    payload->animatorHandle = nullptr;

    // Null the caller's pointer now — the onCancel callback (fired by Cancel)
    // will also try to null it via backPtr, which is a safe no-op.
    payload = nullptr;

    if (handle != nullptr) {
        OH_ArkUI_Animator_Cancel(handle);
        ArkUI_NativeAnimateAPI_1* api = getAnimateApi();
        if (api != nullptr) {
            api->disposeAnimator(handle);
        }
    }
    // The onCancel callback (fired synchronously or on next frame) will:
    //   1. See destroyed=true → skip node access
    //   2. Null *(p->backPtr) → no-op (already nulled above)
    //   3. disposeAnimator   → no-op (already disposed)
    //   4. delete p          → frees the payload
    // So we do NOT delete the payload here — the callback owns deletion.
}

} // namespace a2ui
