package com.amap.agenui.render.component;

import androidx.annotation.RestrictTo;

/**
 * Bridge interface between components and the Native layer.
 * <p>
 * Responsible for submitting component Action events and synchronizing UI state data to the
 * Native layer.
 * <p>
 * Implemented by SurfaceManager and injected via Surface → A2UIComponent.
 * <p>
 * <b>For internal SDK use only; not exposed externally.</b>
 *
 */
@RestrictTo(RestrictTo.Scope.LIBRARY_GROUP)
public interface ComponentEventDispatcher {

    /**
     * Submits a UI Action to the Native layer
     *
     * @param surfaceId   Surface ID
     * @param componentId ID of the component that triggered the action
     * @param contextJson Context parameters (JSON format)
     */
    void submitUIAction(String surfaceId, String componentId, String contextJson);

    /**
     * Submits a UI data model change to the Native layer
     *
     * @param surfaceId   Surface ID
     * @param componentId Component ID
     * @param changeData  Changed content (JSON format)
     */
    void submitUIDataModel(String surfaceId, String componentId, String changeData);

    /**
     * Reports the result of a blank-screen check to the manager layer.
     * Called for both pass ({@code isBlank = false}) and fail ({@code isBlank = true}) outcomes.
     *
     * @param surfaceId Surface ID
     * @param isBlank   {@code true} if the component count is below threshold; {@code false} if the check passed
     */
    void onBlankCheckResult(String surfaceId, boolean isBlank);
}
