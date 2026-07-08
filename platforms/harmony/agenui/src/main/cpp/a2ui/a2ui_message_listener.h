#pragma once

#include "agenui_message_listener.h"
#include "render/a2ui_surface_manager.h"
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <napi/native_api.h>

namespace agenui {

// ==================== Main-thread Dispatch ====================

/**
 * @brief Task wrapper for NAPI work that must run on the main thread.
 */
using MainThreadTask = std::function<void(napi_env)>;

/**
 * @brief Multi-instance A2UI message listener.
 * @remark Each ISurfaceManager owns one A2UIMessageListener instance used to
 *         receive C++ engine events and render them into the A2UI container.
 *
 * Data flow:
 * - onCreateSurface: create the surface with its own registry and component tree
 * - onDeleteSurface: destroy the surface and recursively tear down the component tree
 */
// Inherits enable_shared_from_this so worker-thread events can capture a
// weak_ptr and safely no-op when the listener has already been destroyed
// on the main thread (see postToMainThread call sites below).
class A2UIMessageListener : public IAGenUIMessageListener,
                            public std::enable_shared_from_this<A2UIMessageListener> {
public:
    /**
     * @brief Constructor
     * @param instanceId Associated ISurfaceManager instance ID
     */
    explicit A2UIMessageListener(int instanceId);
    ~A2UIMessageListener();
    
    // Non-copyable
    A2UIMessageListener(const A2UIMessageListener&) = delete;
    A2UIMessageListener& operator=(const A2UIMessageListener&) = delete;
    
    /**
     * @brief Set the threadsafe function.
     * @param tsfn Global main-thread threadsafe function
     */
    void setTsfn(napi_threadsafe_function tsfn);
    
    /**
     * @brief Return the associated instanceId.
     */
    int getInstanceId() const { return instanceId_; }

    // ==================== IAGenUIMessageListener Implementation ====================

    void onCreateSurface(const CreateSurfaceMessage& msg) override;
    void onDeleteSurface(const DeleteSurfaceMessage& msg) override;
    void onActionEventRouted(const std::string &content) override;
    void onComponentsUpdate(const std::string& surfaceId, const std::vector<ComponentsUpdateMessage>& msg) override;
    void onComponentsAdd(const std::string& surfaceId, const std::vector<ComponentsAddMessage>& msg) override;
    void onComponentsRemove(const std::string& surfaceId, const std::vector<ComponentsRemoveMessage>& msg) override;
    void onError(const ErrorMessage& msg) override;

    // ==================== Render-layer Access ====================

    /**
     * @brief Return the render-layer surface manager.
     */
    a2ui::A2UISurfaceManager* getSurfaceManager() const;

    /**
     * @brief Dispatch component appeared event to ArkTS listeners.
     * @param surfaceId Surface ID
     * @param parentComponentId Container component ID
     * @param parentType Container type name
     * @param properties Child's raw properties JSON string
     */
    void dispatchComponentAppeared(const std::string& surfaceId,
                                   const std::string& parentComponentId,
                                   const std::string& parentType,
                                   const std::string& properties);

    /**
     * @brief Dispatch root component property update to ArkTS listeners.
     * @param surfaceId Surface ID
     * @param propsJson Root component properties as JSON string
     */
    void dispatchRootComponentUpdate(const std::string& surfaceId,
                                     const std::string& propsJson);

    /**
     * @brief Deprecated contentHandle-ready callback.
     * @deprecated Use bindSurface instead.
     */
    void onContentHandleReady();

    // ==================== ArkTS Listener Management ====================

    /**
     * @brief Register an A2UI surface listener.
     * @param listener NAPI listener object
     */
    void registerListener(napi_value listener);

    /**
     * @brief Unregister the A2UI surface listener
     * @param listener NAPI listener object
     */
    void unregisterListener(napi_value listener);

    // ==================== Instance-based Lookup ====================

    /**
     * @brief Look up a listener by its owning SurfaceManager's instance ID.
     * @param instanceId SurfaceManager instance ID
     * @return Matching listener, or nullptr when not found
     */
    static A2UIMessageListener* findListenerByInstanceId(int instanceId);

private:
    int instanceId_;                                  // Associated ISurfaceManager instance ID
    std::unique_ptr<a2ui::A2UISurfaceManager> surfaceManager_;  // Render-layer surface manager
    std::vector<napi_ref> listeners_;               // Registered ArkTS listeners

    // Main-thread threadsafe function owned by napi_init.cpp Init/Stop.
    napi_threadsafe_function tsfn_ = nullptr;

    /**
     * @brief Dispatch a task to the main thread.
     * @param task Operation that must run on the main thread
     */
    void postToMainThread(MainThreadTask task);

    // Shared instanceId -> A2UIMessageListener* lookup for exposure dispatch.
    static std::map<int, A2UIMessageListener*>& getInstanceToListenerMap();
    static std::mutex s_instanceMapMutex_;
};

} // namespace agenui
