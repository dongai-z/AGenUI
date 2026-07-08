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
 * RISK60: TextStreamPlugin continueDataModelStreaming() surfaceId type-mismatch crash
 *
 * VULNERABILITY:
 * In agenui_text_stream_plugin.cpp::continueDataModelStreaming() (line 306):
 *   std::string sid = udm["surfaceId"].get<std::string>();
 *
 * When the streaming data model JSON completes, the plugin re-parses the full
 * JSON and calls .get<std::string>() on the "surfaceId" field WITHOUT checking
 * if it is actually a string. If surfaceId is integer/null/array, nlohmann throws
 * type_error → std::terminate() → SIGABRT.
 *
 * DIFFERENTIATION from RISK40-59 (same bug class, DIFFERENT code path):
 * - Location: stream/ layer (TextStreamPlugin), NOT surface/component_manager
 * - Trigger: Multi-chunk streaming with pre-registered Text data binding
 * - Entry: handleIncompleteDataModel() uses TEXT-BASED extraction (silently ignores
 *   non-string surfaceId), then continueDataModelStreaming() uses JSON parsing (crashes)
 * - Independent fix needed: patching component_manager/surface does NOT protect this path
 *
 * ATTACK FLOW:
 * 1. Send updateComponents with Text component having data binding path
 *    → registers path in _textBindingPaths
 * 2. Send INCOMPLETE updateDataModel with integer surfaceId + matching path
 *    → handleIncompleteDataModel enters streaming mode
 * 3. Send completion of the JSON
 *    → continueDataModelStreaming re-parses and crashes on surfaceId.get<string>()
 *
 * Shared core/ code — affects Android, iOS, and HarmonyOS.
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeStreamPluginSurfaceIdCrashTest {

    private static final String TAG = "RISK60_StreamPlugin";

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
     * Test 1: surfaceId as integer in streaming data model completion.
     *
     * Attack sequence:
     * 1. Create surface + Text component with data binding "text":{"path":"/msg"}
     * 2. Send incomplete updateDataModel with "surfaceId":12345 and "value":"Hello worl
     *    (incomplete string → plugin enters streaming mode)
     * 3. Send 'd"}}' to complete the JSON
     *    → continueDataModelStreaming() re-parses full JSON
     *    → udm["surfaceId"].get<std::string>() throws on integer
     *    → SIGABRT
     */
    @Test(timeout = 30000)
    public void test_streamingSurfaceIdInteger() throws Exception {
        Log.i(TAG, "=== RISK60 Test 1: streaming DM surfaceId=12345 (integer) ===");
        String surfaceId = "s_r60_t1";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Step 1: Register a Text component with data binding path "/msg"
        // This populates _textBindingPaths in the TextStreamPlugin
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Column\",\"children\":[\"t1\"]},"
                + "{\"id\":\"t1\",\"component\":\"Text\",\"text\":{\"path\":\"/msg\"}}"
                + "]}}";

        Log.i(TAG, "Step 1: Registering Text component with data binding path /msg");
        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        Thread.sleep(500); // Allow worker thread to process

        // Step 2: Send INCOMPLETE updateDataModel with integer surfaceId
        // The value is an incomplete string — triggers TextStreamPlugin streaming mode
        // extractStringValue(buffer, "surfaceId") returns "" (no quoted value after surfaceId:)
        // extractStringValue(buffer, "path") returns "/msg" (matches binding path)
        // locateNestedStringValue finds "value":"Hello worl (incomplete string)
        // → Plugin enters streaming mode
        String incompleteChunk = "{\"version\":\"v0.9\",\"updateDataModel\":"
                + "{\"surfaceId\":12345,\"path\":\"/msg\",\"value\":\"Hello worl";

        Log.i(TAG, "Step 2: Sending incomplete updateDataModel with integer surfaceId");
        sm.receiveTextChunk(incompleteChunk);
        Thread.sleep(300); // Allow plugin to enter streaming mode

        // Step 3: Complete the JSON — triggers continueDataModelStreaming()
        // Buffer is now complete: {"version":"v0.9","updateDataModel":{"surfaceId":12345,"path":"/msg","value":"Hello world"}}
        // continueDataModelStreaming() re-parses full JSON via nlohmann::json::parse
        // udm["surfaceId"].get<std::string>() on integer 12345 → type_error → terminate → SIGABRT
        String completionChunk = "d\"}}";

        Log.i(TAG, "Step 3: Completing JSON — expecting crash in continueDataModelStreaming()");
        sm.receiveTextChunk(completionChunk);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 1 survived (unexpected if bug exists) — process still alive");
    }

    /**
     * Test 2: surfaceId as null in streaming data model completion.
     *
     * Same multi-chunk attack but with "surfaceId":null
     */
    @Test(timeout = 30000)
    public void test_streamingSurfaceIdNull() throws Exception {
        Log.i(TAG, "=== RISK60 Test 2: streaming DM surfaceId=null ===");
        String surfaceId = "s_r60_t2";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Step 1: Register Text with data binding
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Column\",\"children\":[\"t1\"]},"
                + "{\"id\":\"t1\",\"component\":\"Text\",\"text\":{\"path\":\"/msg\"}}"
                + "]}}";

        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        Thread.sleep(500);

        // Step 2: Incomplete updateDataModel with null surfaceId
        String incompleteChunk = "{\"version\":\"v0.9\",\"updateDataModel\":"
                + "{\"surfaceId\":null,\"path\":\"/msg\",\"value\":\"Stream tex";

        Log.i(TAG, "Step 2: Sending incomplete chunk with null surfaceId");
        sm.receiveTextChunk(incompleteChunk);
        Thread.sleep(300);

        // Step 3: Complete
        String completionChunk = "t\"}}";

        Log.i(TAG, "Step 3: Completing JSON — expecting crash");
        sm.receiveTextChunk(completionChunk);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 2 survived (unexpected if bug exists)");
    }

    /**
     * Test 3: path as array in streaming data model completion.
     *
     * Tests the second unguarded .get<std::string>() at line 308:
     *   std::string ePath = udm.contains("path") ? udm["path"].get<std::string>() : "/";
     *
     * surfaceId is valid string, but path is [1,2,3] (array)
     */
    @Test(timeout = 30000)
    public void test_streamingPathArray() throws Exception {
        Log.i(TAG, "=== RISK60 Test 3: streaming DM path=[1,2,3] (array) ===");
        String surfaceId = "s_r60_t3";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Step 1: Register Text with data binding at "/"
        // Using "/" as the binding path: isPathPrefix("/", "/") returns true
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Column\",\"children\":[\"t1\"]},"
                + "{\"id\":\"t1\",\"component\":\"Text\",\"text\":{\"path\":\"/\"}}"
                + "]}}";

        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        Thread.sleep(500);

        // Step 2: Incomplete updateDataModel with array path
        // extractStringValue(buffer, "path") will fail (value is [1,2,3] not quoted)
        // → rawPath = "" → normalizePath("") → "/"
        // isPathPrefix("/", "/") → true (all paths match when eventPath is "/")
        // locateNestedStringValue finds "value":"Crashing tex (incomplete)
        // → streaming starts
        String incompleteChunk = "{\"version\":\"v0.9\",\"updateDataModel\":"
                + "{\"surfaceId\":\"" + surfaceId + "\",\"path\":[1,2,3],\"value\":\"Crashing tex";

        Log.i(TAG, "Step 2: Sending incomplete chunk with array path");
        sm.receiveTextChunk(incompleteChunk);
        Thread.sleep(300);

        // Step 3: Complete — hits udm["path"].get<std::string>() on array type
        String completionChunk = "t\"}}";

        Log.i(TAG, "Step 3: Completing JSON — expecting crash at path.get<string>()");
        sm.receiveTextChunk(completionChunk);
        sm.endTextStream();

        Thread.sleep(3000);
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
