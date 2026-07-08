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

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * RISK38: Deep component tree (NOT deep JSON nesting) → VirtualDOM/Yoga stack overflow.
 *
 * DIFFERENCE FROM RISK32:
 * - RISK32 tested JSON PARSER recursion (deeply nested {"a":{"a":{...}}} objects)
 * - RISK38 tests COMPONENT TREE recursion: JSON is a FLAT array, but components form
 *   a deep parent→child chain. After parsing (which is O(n) iteration), the layout engine
 *   traverses the tree recursively:
 *   - VirtualDOMNode::checkAndNotifyLayoutChanges() recurses through tree depth
 *   - YGNodeCalculateLayout recurses through Yoga tree depth
 *   - VirtualDOMNode::refreshChildrenRecursively() recurses
 *
 * Each recursion frame ≈ 500-800 bytes. Worker thread stack ≈ 1MB.
 * At 2000+ depth → stack overflow → SIGSEGV.
 *
 * The JSON structure is:
 * {"updateComponents":{"surfaceId":"s1","components":[
 *   {"id":"root","component":"Column","children":["c1"]},
 *   {"id":"c1","component":"Column","children":["c2"]},
 *   {"id":"c2","component":"Column","children":["c3"]},
 *   ...
 *   {"id":"cN","component":"Text","content":"leaf"}
 * ]}}
 *
 * This is a perfectly valid flat JSON array (no parser depth risk), but produces
 * a component tree of depth N that overflows the layout traversal stack.
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeDeepComponentTreeTest {

    private static final String TAG = "RISK38_DeepTree";

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
        // Don't destroy engine - other tests may still need it
    }

    /**
     * Test 1: 2000-level deep component tree.
     * Worker thread stack is ~1MB. Each VirtualDOMNode::checkAndNotifyLayoutChanges()
     * frame is ~500-800B. At 2000 depth → 1-1.6MB → should overflow.
     */
    @Test(timeout = 60000)
    public void test_deepComponentTree_2000_levels() throws Exception {
        Log.i(TAG, "=== Test 1: 2000-level deep component tree ===");
        exerciseDeepTree(2000);
    }

    /**
     * Test 2: 5000-level deep component tree.
     * Even if 2000 doesn't overflow, 5000 should definitely exceed any thread stack.
     */
    @Test(timeout = 60000)
    public void test_deepComponentTree_5000_levels() throws Exception {
        Log.i(TAG, "=== Test 2: 5000-level deep component tree ===");
        exerciseDeepTree(5000);
    }

    /**
     * Test 3: 10000-level deep component tree.
     * Maximum pressure - guaranteed to exceed any reasonable stack size.
     */
    @Test(timeout = 60000)
    public void test_deepComponentTree_10000_levels() throws Exception {
        Log.i(TAG, "=== Test 3: 10000-level deep component tree ===");
        exerciseDeepTree(10000);
    }

    /**
     * Test 4: Deep tree via streaming (chunked delivery).
     * Same depth but delivered in multiple receiveTextChunk calls,
     * verifying the crash happens during layout not during parsing.
     */
    @Test(timeout = 60000)
    public void test_deepComponentTree_streaming_3000_levels() throws Exception {
        Log.i(TAG, "=== Test 4: 3000-level deep tree via streaming ===");
        exerciseDeepTreeStreaming(3000);
    }

    private void exerciseDeepTree(int depth) throws Exception {
        final SurfaceManager sm = new SurfaceManager(activity);
        if (sm == null) {
            Log.e(TAG, "Failed to create SurfaceManager");
            return;
        }

        // Step 1: Create surface
        String createSurface = "{\"createSurface\":{\"surfaceId\":\"s1\",\"catalogId\":\"test\"}}";
        sm.beginTextStream();
        sm.receiveTextChunk(createSurface);
        sm.endTextStream();

        // Wait for surface creation to complete on worker thread
        Thread.sleep(200);

        // Step 2: Build the deep component tree JSON (flat array, deep tree structure)
        String updateJson = buildDeepTreeJson(depth, "s1");
        Log.i(TAG, "JSON size: " + updateJson.length() + " bytes, depth: " + depth);

        // Step 3: Send the updateComponents message
        AtomicBoolean crashed = new AtomicBoolean(false);
        try {
            sm.beginTextStream();
            sm.receiveTextChunk(updateJson);
            sm.endTextStream();
        } catch (Exception e) {
            Log.e(TAG, "Exception during streaming: " + e.getMessage());
            crashed.set(true);
        }

        // Step 4: Wait for the worker thread to process and trigger layout
        // The crash (if any) happens asynchronously on the worker thread during
        // checkAndNotifyLayoutChanges() or YGNodeCalculateLayout()
        Thread.sleep(5000);

        Log.i(TAG, "Survived depth=" + depth + " (no crash detected from test thread)");

        // Cleanup
        try {
            sm.destroy();
        } catch (Exception e) {
            Log.w(TAG, "destroy() exception: " + e.getMessage());
        }
    }

    private void exerciseDeepTreeStreaming(int depth) throws Exception {
        final SurfaceManager sm = new SurfaceManager(activity);
        if (sm == null) {
            Log.e(TAG, "Failed to create SurfaceManager");
            return;
        }

        // Step 1: Create surface
        String createSurface = "{\"createSurface\":{\"surfaceId\":\"s1\",\"catalogId\":\"test\"}}";
        sm.beginTextStream();
        sm.receiveTextChunk(createSurface);
        sm.endTextStream();

        Thread.sleep(200);

        // Step 2: Build and send in chunks (simulate real streaming)
        String updateJson = buildDeepTreeJson(depth, "s1");
        int chunkSize = 8192; // 8KB chunks

        Log.i(TAG, "Streaming " + updateJson.length() + " bytes in " +
                ((updateJson.length() + chunkSize - 1) / chunkSize) + " chunks");

        sm.beginTextStream();
        for (int offset = 0; offset < updateJson.length(); offset += chunkSize) {
            int end = Math.min(offset + chunkSize, updateJson.length());
            sm.receiveTextChunk(updateJson.substring(offset, end));
        }
        sm.endTextStream();

        // Wait for layout processing
        Thread.sleep(5000);

        Log.i(TAG, "Streaming survived depth=" + depth);

        try {
            sm.destroy();
        } catch (Exception e) {
            Log.w(TAG, "destroy() exception: " + e.getMessage());
        }
    }

    /**
     * Builds a flat JSON array that creates a deeply nested component tree.
     * Uses "Text" (non-template) with "children" field to form the deep chain.
     * Includes "version":"v0.9" as required by the streaming protocol.
     */
    private String buildDeepTreeJson(int depth, String surfaceId) {
        StringBuilder sb = new StringBuilder(depth * 100);
        sb.append("{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"").append(surfaceId)
          .append("\",\"components\":[");

        // Root component with child reference
        sb.append("{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"c_0\"],\"attributes\":{\"value\":\"root\"}}");

        // Chain of Text components, each with a single child
        for (int i = 0; i < depth - 1; i++) {
            sb.append(",{\"id\":\"c_").append(i)
              .append("\",\"component\":\"Text\",\"children\":[\"c_").append(i + 1)
              .append("\"],\"attributes\":{\"value\":\"n").append(i)
              .append("\"}}");
        }

        // Leaf component (no children)
        sb.append(",{\"id\":\"c_").append(depth - 1)
          .append("\",\"component\":\"Text\",\"attributes\":{\"value\":\"leaf\"}}");

        sb.append("]}}")
        ;
        return sb.toString();
    }
}
