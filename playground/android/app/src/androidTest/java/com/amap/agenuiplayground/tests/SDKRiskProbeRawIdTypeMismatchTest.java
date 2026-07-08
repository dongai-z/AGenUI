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
 * RISK59: parseComponent "rawId" type-mismatch → nlohmann::type_error → SIGABRT
 *
 * VULNERABILITY (agenui_component_manager.cpp line 347-348):
 *   std::string rawId = id;
 *   if (json.contains("rawId")) {
 *       rawId = json["rawId"].get<std::string>();  // ← THROWS if rawId is non-string
 *   }
 *
 * The "rawId" field is an internal tracking field. parseComponent only checks
 * contains("rawId") but does NOT verify is_string() before calling get<std::string>().
 *
 * DISTINCT from RISK40 because:
 * - RISK40 crashes on json["id"].get<std::string>() when id is null/non-string
 * - RISK59 crashes on json["rawId"].get<std::string>() when rawId is non-string,
 *   with id being a VALID string that passes all existing checks
 * - A fix for RISK40 (adding is_string() for id) would NOT fix rawId
 *
 * Attack passes Surface::updateComponents() validation because:
 * - "component" field is a valid string ("Text") → passes line 176 check
 * - "id" field is a valid string → tryApplyTextChunk finds no "textChunk" → returns false
 * - Falls through to parseComponent which succeeds on id/component but crashes on rawId
 *
 * Shared core/ code — affects Android, iOS, and HarmonyOS.
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeRawIdTypeMismatchTest {

    private static final String TAG = "RISK59_RawIdType";

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
     * Test 1: "rawId" as integer with valid string "id" and "component".
     *
     * Flow:
     * 1. Surface::updateComponents → "component" is valid string → passes
     * 2. ComponentManager::tryApplyTextChunk → no "textChunk" field → returns false
     * 3. ComponentManager::parseComponent:
     *    - json["id"].get<std::string>() → "valid_c1" → OK
     *    - json["component"].get<std::string>() → "Text" → OK
     *    - json.contains("rawId") → true
     *    - json["rawId"].get<std::string>() → THROWS type_error (12345 is number)
     * 4. Exception uncaught → std::terminate → SIGABRT
     */
    @Test(timeout = 30000)
    public void test_rawIdInteger() throws Exception {
        Log.i(TAG, "=== RISK59 Test 1: rawId as integer ===");
        String surfaceId = "s_r59_t1";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Attack: valid id (string) + valid component (string) + rawId as integer
        String attackJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"valid_c1\",\"component\":\"Text\",\"rawId\":12345}"
                + "]}}";

        Log.i(TAG, "Attack: sending component with rawId=12345 (integer)");
        sm.beginTextStream();
        sm.receiveTextChunk(attackJson);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 1 survived (unexpected if bug exists) — process still alive");
    }

    /**
     * Test 2: "rawId" as null with valid string "id" and "component".
     *
     * json["rawId"].get<std::string>() throws on null type.
     */
    @Test(timeout = 30000)
    public void test_rawIdNull() throws Exception {
        Log.i(TAG, "=== RISK59 Test 2: rawId as null ===");
        String surfaceId = "s_r59_t2";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Attack: valid id + valid component + rawId=null
        String attackJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"valid_c2\",\"component\":\"Text\",\"rawId\":null}"
                + "]}}";

        Log.i(TAG, "Attack: sending component with rawId=null");
        sm.beginTextStream();
        sm.receiveTextChunk(attackJson);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 2 survived (unexpected if bug exists)");
    }

    /**
     * Test 3: "rawId" as array with valid string "id" and "component".
     *
     * json["rawId"].get<std::string>() throws on array type.
     */
    @Test(timeout = 30000)
    public void test_rawIdArray() throws Exception {
        Log.i(TAG, "=== RISK59 Test 3: rawId as array ===");
        String surfaceId = "s_r59_t3";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Attack: valid id + valid component + rawId as array
        String attackJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"valid_c3\",\"component\":\"Column\",\"rawId\":[1,2,3]}"
                + "]}}";

        Log.i(TAG, "Attack: sending component with rawId=[1,2,3]");
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
