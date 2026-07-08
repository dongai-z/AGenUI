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
 * RISK39: Extreme CSS numeric values → Yoga INFINITY/NaN → crash or infinite layout loop.
 *
 * VULNERABILITY:
 * parseCssFloat() uses std::strtof which produces INFINITY for numbers > FLT_MAX (~3.4e38).
 * The result is passed directly to YGNodeStyleSet* without any validation.
 *
 * ATTACK VECTORS:
 * 1. flex-grow = INFINITY for two siblings:
 *    - total_flex = INFINITY + INFINITY = INFINITY
 *    - space_per_grow = remaining / INFINITY = 0
 *    - item_size = flex_basis + INFINITY * 0 = NaN (IEEE 754: INFINITY * 0 = NaN)
 *
 * 2. width/height = INFINITY:
 *    - Layout results propagate INFINITY to platform layer
 *    - (int)INFINITY or (int)NaN = undefined behavior in C++
 *
 * 3. padding = INFINITY:
 *    - available_inner = width - 2*INFINITY = -INFINITY
 *    - Child layout with -INFINITY available width
 *
 * 4. Conflicting constraints:
 *    - min-width = INFINITY with max-width = 0
 *    - Yoga internal constraint resolution with extreme values
 *
 * The 63-digit string "999...9" fits in parseCssFloat's 64-byte buffer
 * and strtof returns HUGE_VALF (INFINITY). No overflow check exists.
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeExtremeStyleValuesTest {

    private static final String TAG = "RISK39_ExtremeStyle";

    // 63 nines: fits in 64-byte buffer, strtof produces INFINITY
    private static final String INFINITY_STR =
            "999999999999999999999999999999999999999999999999999999999999999";

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
     * Test 1: Two siblings with flex-grow = INFINITY.
     *
     * Expected NaN generation path:
     * - total_flex_grow = INF + INF = INF
     * - space_per_grow = remaining_space / INF = 0
     * - child_size = flex_basis + INF * 0 = NaN  (IEEE 754)
     *
     * NaN in layout → platform (int)NaN = UB → potential crash/ANR
     */
    @Test(timeout = 30000)
    public void test_infinityFlexGrow_NaN_generation() throws Exception {
        Log.i(TAG, "=== Test 1: INFINITY flex-grow → NaN via INF*0 ===");
        String surfaceId = "s_r39_t1";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Parent: row layout with fixed width, children with INFINITY flex-grow
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"a\",\"b\"],"
                + "\"attributes\":{\"value\":\"root\"},"
                + "\"styles\":{\"width\":\"300\",\"height\":\"200\",\"flex-direction\":\"row\"}},"
                + "{\"id\":\"a\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"A\"},"
                + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"b\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"B\"},"
                + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\"}}"
                + "]}}";

        Log.i(TAG, "Sending updateComponents with INFINITY flex-grow, JSON length=" + json.length());
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        // Wait for layout calculation
        Thread.sleep(2000);
        Log.i(TAG, "Test 1 survived initial layout. Triggering relayout...");

        // Force multiple relayouts to amplify NaN propagation effects
        for (int i = 0; i < 5; i++) {
            String update = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                    + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"a\",\"b\"],"
                    + "\"attributes\":{\"value\":\"relayout" + i + "\"},"
                    + "\"styles\":{\"width\":\"" + (300 + i) + "\",\"height\":\"200\",\"flex-direction\":\"row\"}},"
                    + "{\"id\":\"a\",\"component\":\"Text\","
                    + "\"attributes\":{\"value\":\"A" + i + "\"},"
                    + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\"}},"
                    + "{\"id\":\"b\",\"component\":\"Text\","
                    + "\"attributes\":{\"value\":\"B" + i + "\"},"
                    + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\"}}"
                    + "]}}";
            sm.beginTextStream();
            sm.receiveTextChunk(update);
            sm.endTextStream();
            Thread.sleep(500);
        }
        Log.i(TAG, "Test 1 completed without visible crash");
    }

    /**
     * Test 2: Width and height set to INFINITY.
     *
     * INFINITY layout dimensions → platform layer receives INFINITY →
     * integer conversion UB, or view system crash on impossibly large frame.
     */
    @Test(timeout = 30000)
    public void test_infinityDimensions() throws Exception {
        Log.i(TAG, "=== Test 2: INFINITY width/height ===");
        String surfaceId = "s_r39_t2";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"child\"],"
                + "\"attributes\":{\"value\":\"root\"},"
                + "\"styles\":{\"width\":\"" + INFINITY_STR + "\",\"height\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"child\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"child\"},"
                + "\"styles\":{\"width\":\"100\",\"height\":\"50\"}}"
                + "]}}";

        Log.i(TAG, "Sending INFINITY dimensions, JSON length=" + json.length());
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 2 completed without visible crash");
    }

    /**
     * Test 3: INFINITY padding on all sides.
     *
     * available_inner_width = container_width - padding_left - padding_right
     *                       = 300 - INF - INF = -INF
     * Children layout with -INF available → potential assertion/crash in Yoga
     */
    @Test(timeout = 30000)
    public void test_infinityPadding() throws Exception {
        Log.i(TAG, "=== Test 3: INFINITY padding → negative inner space ===");
        String surfaceId = "s_r39_t3";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"inner\"],"
                + "\"attributes\":{\"value\":\"root\"},"
                + "\"styles\":{\"width\":\"300\",\"height\":\"200\","
                + "\"padding\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"inner\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"inner\"},"
                + "\"styles\":{\"width\":\"100%\",\"height\":\"50\"}}"
                + "]}}";

        Log.i(TAG, "Sending INFINITY padding, JSON length=" + json.length());
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 3 completed without visible crash");
    }

    /**
     * Test 4: Conflicting INFINITY constraints (min-width=INF, max-width=0).
     *
     * Yoga resolves: final = max(min, min(computed, max)) = max(INF, 0) = INF
     * Combined with flex-shrink and bounded parent → potential NaN/assertion
     */
    @Test(timeout = 30000)
    public void test_conflictingInfinityConstraints() throws Exception {
        Log.i(TAG, "=== Test 4: Conflicting INFINITY min/max constraints ===");
        String surfaceId = "s_r39_t4";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"c1\",\"c2\"],"
                + "\"attributes\":{\"value\":\"root\"},"
                + "\"styles\":{\"width\":\"300\",\"height\":\"200\",\"flex-direction\":\"row\"}},"
                + "{\"id\":\"c1\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"c1\"},"
                + "\"styles\":{\"min-width\":\"" + INFINITY_STR + "\",\"max-width\":\"0\","
                + "\"flex-shrink\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"c2\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"c2\"},"
                + "\"styles\":{\"width\":\"100\",\"flex-grow\":\"1\"}}"
                + "]}}";

        Log.i(TAG, "Sending conflicting constraints, JSON length=" + json.length());
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 4 completed without visible crash");
    }

    /**
     * Test 5: INFINITY aspect-ratio combined with fixed height.
     *
     * aspect-ratio = INF → computed_width = height * INF = INF
     * Then flex container tries to shrink INF → potential NaN
     */
    @Test(timeout = 30000)
    public void test_infinityAspectRatio() throws Exception {
        Log.i(TAG, "=== Test 5: INFINITY aspect-ratio ===");
        String surfaceId = "s_r39_t5";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Note: aspect-ratio parsing checks > 0.0f, and INF > 0 is true
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"ar\"],"
                + "\"attributes\":{\"value\":\"root\"},"
                + "\"styles\":{\"width\":\"300\",\"height\":\"200\",\"flex-direction\":\"row\"}},"
                + "{\"id\":\"ar\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"ar\"},"
                + "\"styles\":{\"height\":\"100\",\"aspect-ratio\":\"" + INFINITY_STR + "\","
                + "\"flex-shrink\":\"" + INFINITY_STR + "\"}}"
                + "]}}";

        Log.i(TAG, "Sending INFINITY aspect-ratio, JSON length=" + json.length());
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 5 completed without visible crash");
    }

    /**
     * Test 6: Multiple INFINITY properties combined - maximum NaN propagation.
     *
     * Combines INFINITY flex-grow, padding, and margin on nested components
     * to maximize the chance of NaN generation through multiple arithmetic paths.
     */
    @Test(timeout = 30000)
    public void test_combinedInfinityProperties() throws Exception {
        Log.i(TAG, "=== Test 6: Combined INFINITY properties for max NaN propagation ===");
        String surfaceId = "s_r39_t6";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"p1\",\"p2\",\"p3\"],"
                + "\"attributes\":{\"value\":\"root\"},"
                + "\"styles\":{\"width\":\"400\",\"height\":\"300\",\"flex-direction\":\"row\"}},"
                + "{\"id\":\"p1\",\"component\":\"Text\",\"children\":[\"inner1\"],"
                + "\"attributes\":{\"value\":\"p1\"},"
                + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\","
                + "\"padding\":\"" + INFINITY_STR + "\","
                + "\"margin\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"p2\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"p2\"},"
                + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\","
                + "\"min-width\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"p3\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"p3\"},"
                + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\","
                + "\"flex-basis\":\"" + INFINITY_STR + "\"}},"
                + "{\"id\":\"inner1\",\"component\":\"Text\","
                + "\"attributes\":{\"value\":\"inner\"},"
                + "\"styles\":{\"width\":\"50\",\"height\":\"50\"}}"
                + "]}}";

        Log.i(TAG, "Sending combined INFINITY properties, JSON length=" + json.length());
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);

        // Force relayout to amplify any NaN cache issues
        for (int i = 0; i < 3; i++) {
            String update = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                    + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"p1\",\"p2\",\"p3\"],"
                    + "\"attributes\":{\"value\":\"relayout" + i + "\"},"
                    + "\"styles\":{\"width\":\"" + (400 + i * 10) + "\",\"height\":\"300\",\"flex-direction\":\"row\"}},"
                    + "{\"id\":\"p1\",\"component\":\"Text\",\"children\":[\"inner1\"],"
                    + "\"attributes\":{\"value\":\"p1_" + i + "\"},"
                    + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\","
                    + "\"padding\":\"" + INFINITY_STR + "\","
                    + "\"margin\":\"" + INFINITY_STR + "\"}},"
                    + "{\"id\":\"p2\",\"component\":\"Text\","
                    + "\"attributes\":{\"value\":\"p2_" + i + "\"},"
                    + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\","
                    + "\"min-width\":\"" + INFINITY_STR + "\"}},"
                    + "{\"id\":\"p3\",\"component\":\"Text\","
                    + "\"attributes\":{\"value\":\"p3_" + i + "\"},"
                    + "\"styles\":{\"flex-grow\":\"" + INFINITY_STR + "\","
                    + "\"flex-basis\":\"" + INFINITY_STR + "\"}},"
                    + "{\"id\":\"inner1\",\"component\":\"Text\","
                    + "\"attributes\":{\"value\":\"inner_" + i + "\"},"
                    + "\"styles\":{\"width\":\"50\",\"height\":\"50\"}}"
                    + "]}}";
            sm.beginTextStream();
            sm.receiveTextChunk(update);
            sm.endTextStream();
            Thread.sleep(500);
        }
        Log.i(TAG, "Test 6 completed without visible crash");
    }

    // ===== Helper methods =====

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
