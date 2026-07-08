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
 * RISK40: JSON type-mismatch in component fields → nlohmann json throws
 * uncaught C++ exception → std::terminate() → SIGABRT.
 *
 * VULNERABILITY:
 * In agenui_component_manager.cpp:parseComponent():
 *   if (!json.contains("id") || !json.contains("component")) {
 *       return nullptr;  // Only checks key EXISTS, not its type
 *   }
 *   std::string id = json["id"].get<std::string>();         // THROWS if non-string
 *   std::string component = json["component"].get<std::string>(); // THROWS if non-string
 *
 * nlohmann::json::get<std::string>() throws json::type_error when value
 * is not a string (e.g., integer, boolean, null, array, object).
 *
 * In native Android (JNI), uncaught C++ exceptions cross the JNI boundary
 * and result in std::terminate() → SIGABRT (signal 6).
 *
 * ATTACK VECTORS:
 * 1. "component": 12345 (integer where string expected)
 * 2. "id": null (null where string expected)
 * 3. "id": ["array"] (array where string expected)
 * 4. "component": true (boolean where string expected)
 * 5. Both "id" and "component" as non-string types
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeJsonTypeMismatchTest {

    private static final String TAG = "RISK40_TypeMismatch";

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
        // Don't destroy engine
    }

    /**
     * Test 1: "component" field as integer (12345 instead of "Text").
     *
     * json.contains("component") → true (key exists)
     * json["component"].get<std::string>() → throws nlohmann::json::type_error
     * → std::terminate() → SIGABRT
     */
    @Test(timeout = 30000)
    public void test_componentFieldAsInteger() throws Exception {
        Log.i(TAG, "=== Test 1: component field as integer ===");
        String surfaceId = "s_r40_t1";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Note: "component": 12345 is valid JSON but type-mismatched
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":12345,\"attributes\":{\"value\":\"test\"}}"
                + "]}}";

        Log.i(TAG, "Sending type-mismatched component (integer), JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 1 survived - checking if crash occurred");
    }

    /**
     * Test 2: "id" field as null.
     *
     * json.contains("id") → true (key exists with null value)
     * json["id"].get<std::string>() → throws on null
     */
    @Test(timeout = 30000)
    public void test_idFieldAsNull() throws Exception {
        Log.i(TAG, "=== Test 2: id field as null ===");
        String surfaceId = "s_r40_t2";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":null,\"component\":\"Text\",\"attributes\":{\"value\":\"test\"}}"
                + "]}}";

        Log.i(TAG, "Sending null id field, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 2 survived");
    }

    /**
     * Test 3: "id" field as array.
     *
     * json.contains("id") → true
     * json["id"].get<std::string>() → throws on array type
     */
    @Test(timeout = 30000)
    public void test_idFieldAsArray() throws Exception {
        Log.i(TAG, "=== Test 3: id field as array ===");
        String surfaceId = "s_r40_t3";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":[\"a\",\"b\"],\"component\":\"Text\",\"attributes\":{\"value\":\"test\"}}"
                + "]}}";

        Log.i(TAG, "Sending array id field, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 3 survived");
    }

    /**
     * Test 4: "component" field as boolean true.
     */
    @Test(timeout = 30000)
    public void test_componentFieldAsBoolean() throws Exception {
        Log.i(TAG, "=== Test 4: component field as boolean ===");
        String surfaceId = "s_r40_t4";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":true,\"attributes\":{\"value\":\"test\"}}"
                + "]}}";

        Log.i(TAG, "Sending boolean component field, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 4 survived");
    }

    /**
     * Test 5: "children" field as string instead of array.
     *
     * In parseChildren: json["children"] is expected to be an array.
     * If it's a string, iteration or .get<std::vector>() would throw.
     */
    @Test(timeout = 30000)
    public void test_childrenFieldAsString() throws Exception {
        Log.i(TAG, "=== Test 5: children field as string ===");
        String surfaceId = "s_r40_t5";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":\"not_an_array\","
                + "\"attributes\":{\"value\":\"test\"}}"
                + "]}}";

        Log.i(TAG, "Sending string children field, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 5 survived");
    }

    /**
     * Test 6: Mix valid and invalid components in one batch.
     * First component is valid (creates surface root), second has type mismatch.
     * This ensures the crash happens mid-processing, not at the start.
     */
    @Test(timeout = 30000)
    public void test_mixedValidAndInvalidComponents() throws Exception {
        Log.i(TAG, "=== Test 6: Mix valid + invalid components ===");
        String surfaceId = "s_r40_t6";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":["
                + "{\"id\":\"root\",\"component\":\"Text\",\"children\":[\"bad\"],"
                + "\"attributes\":{\"value\":\"valid_root\"}},"
                + "{\"id\":\"bad\",\"component\":999,\"attributes\":{\"value\":\"crash_me\"}}"
                + "]}}";

        Log.i(TAG, "Sending mixed valid+invalid components, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 6 survived");
    }

    /**
     * Test 7 (RISK41): Attribute value with {"path": non-string} triggers crash in
     * DataValueParser::parseDataBindingDataValue() — DIFFERENT code path from RISK40.
     * parseDataValue() routes to parseDataBindingDataValue() which checks
     * json.is_object() && json.contains("path") → then does json["path"].get<string>()
     * without is_string() check → throws nlohmann::detail::type_error.
     */
    @Test(timeout = 30000)
    public void test_dataBindingPathNonString() throws Exception {
        Log.i(TAG, "=== Test 7 (RISK41): DataBinding path as array → type_error ===");
        String surfaceId = "s_r41_t7";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // "binding" is not in excluded keys, so it goes through parseDataValue()
        // parseDataValue → parseDataBindingDataValue → json["path"].get<string>() → THROWS
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"t1\",\"component\":\"Text\","
                + "\"binding\":{\"path\":[1,2,3]}}]}}";

        Log.i(TAG, "Sending data binding with array path, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 7 survived (no crash)");
    }

    /**
     * Test 8 (RISK41 variant): Modal component with non-string "trigger" field.
     * parseChildren() for Modal does json["trigger"].get<string>() without type check.
     */
    @Test(timeout = 30000)
    public void test_modalTriggerNonString() throws Exception {
        Log.i(TAG, "=== Test 8: Modal trigger as integer \u2192 type_error ===");
        String surfaceId = "s_r41_t8";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;
    
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"m1\",\"component\":\"Modal\","
                + "\"trigger\":12345}]}}";
    
        Log.i(TAG, "Sending Modal with integer trigger, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();
    
        Thread.sleep(2000);
        Log.i(TAG, "Test 8 survived (no crash)");
    }
    
    /**
     * Test 9 (RISK42): Built-in "regex" function with invalid pattern.
     * Checks attribute triggers automatic function call evaluation during
     * ComponentModel::updateSnapshot(). RegexFunctionCall::execute() at L18
     * calls std::regex(pattern) with an invalid pattern like "((("
     * which throws std::regex_error \u2014 uncaught \u2192 terminate \u2192 SIGABRT.
     *
     * This is a DIFFERENT vulnerability class from RISK40/41 (different exception type,
     * different trigger mechanism, different code path).
     */
    @Test(timeout = 30000)
    public void test_regexInvalidPattern() throws Exception {
        Log.i(TAG, "=== Test 9 (RISK42): Invalid regex pattern via checks ===");
        String surfaceId = "s_r42_t9";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;
    
        // Component with checks that reference the built-in "regex" function
        // with an invalid pattern. The check is automatically evaluated during
        // component snapshot creation (no user interaction needed).
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"input1\",\"component\":\"TextInput\","
                + "\"checks\":[{\"condition\":{\"call\":\"regex\",\"args\":{\"pattern\":\"(((\",\"value\":\"x\"}},\"message\":\"err\"}]}]}}";
    
        Log.i(TAG, "Sending TextInput with invalid regex in checks, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();
    
        Thread.sleep(3000);
        Log.i(TAG, "Test 9 survived (no crash)");
    }

    /**
     * Test 10 (RISK44): ReDoS - catastrophic backtracking freezes worker thread.
     * Uses a valid regex pattern "(a+)+b" with input "aaa...aaa" (30 a's, no b).
     * std::regex_match enters exponential backtracking (2^30 paths), effectively
     * freezing the worker thread forever. This is a FREEZE, not a crash.
     *
     * Detection: test timeout (10s). If the test times out, the worker thread
     * is confirmed frozen by the regex engine.
     */
    @Test(timeout = 10000)
    public void test_regexReDoSFreeze() throws Exception {
        Log.i(TAG, "=== Test 10 (RISK44): ReDoS catastrophic backtracking ===");
        String surfaceId = "s_r44_t10";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Pattern (a+)+b is valid but causes catastrophic backtracking
        // with input that has no 'b' (all a's). 30 a's = 2^30 backtrack paths.
        String evilPattern = "(a+)+b";
        String evilValue = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; // 30 a's, no b

        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"input_redos\",\"component\":\"TextInput\","
                + "\"checks\":[{\"condition\":{\"call\":\"regex\",\"args\":{\"pattern\":\""
                + evilPattern + "\",\"value\":\"" + evilValue
                + "\"}},\"message\":\"validation\"}]}]}}";

        Log.i(TAG, "Sending TextInput with ReDoS pattern, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        // If regex_match freezes, this sleep + subsequent operation will timeout.
        // We send a second component to verify the worker thread is still responsive.
        Thread.sleep(3000);

        // Try to send another operation - if worker is frozen, this won't process
        String json2 = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"normal_comp\",\"component\":\"Text\","
                + "\"value\":\"hello\"}]}}";
        sm.beginTextStream();
        sm.receiveTextChunk(json2);
        sm.endTextStream();

        Thread.sleep(2000);
        Log.i(TAG, "Test 10 completed (worker thread NOT frozen) - ReDoS did not hit");
    }

    /**
     * Test 11 (RISK45): Circular children reference — component declares itself as child.
     * This creates a self-referencing node that could cause infinite recursion in:
     * - VirtualDOM tree building (addChild loops back to same node)
     * - Yoga layout calculation (YGNodeCalculateLayout recurses on cycle)
     * - Tree traversal / destruction
     *
     * Expected: SIGSEGV (stack overflow) or SIGABRT, or safe handling.
     */
    @Test(timeout = 15000)
    public void test_circularChildrenSelfReference() throws Exception {
        Log.i(TAG, "=== Test 11 (RISK45): Circular children self-reference ===");
        String surfaceId = "s_r45_t11";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // Component "loop" declares itself as its own child.
        // This is syntactically valid A2UI JSON but logically creates a cycle.
        String json = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"loop\",\"component\":\"Column\","
                + "\"children\":[\"loop\"]}]}}";

        Log.i(TAG, "Sending self-referencing component, JSON=" + json);
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 11a survived self-reference");

        // Variant 2: Two-node cycle (A → B → A)
        String surfaceId2 = "s_r45_t11b";
        SurfaceManager sm2 = createSurfaceAndWait(surfaceId2);
        if (sm2 == null) return;

        String json2 = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId2
                + "\",\"components\":[{\"id\":\"node_a\",\"component\":\"Column\","
                + "\"children\":[\"node_b\"]},{\"id\":\"node_b\",\"component\":\"Column\","
                + "\"children\":[\"node_a\"]}]}}";

        Log.i(TAG, "Sending two-node cycle, JSON=" + json2);
        sm2.beginTextStream();
        sm2.receiveTextChunk(json2);
        sm2.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 11b survived two-node cycle");

        // Variant 3: Self-reference AND trigger layout by adding a child with styles
        String surfaceId3 = "s_r45_t11c";
        SurfaceManager sm3 = createSurfaceAndWait(surfaceId3);
        if (sm3 == null) return;

        String json3 = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId3
                + "\",\"components\":[{\"id\":\"cyc\",\"component\":\"Column\","
                + "\"style\":{\"width\":\"100%\",\"height\":\"auto\"},"
                + "\"children\":[\"cyc\"]}]}}";

        Log.i(TAG, "Sending styled self-reference, JSON=" + json3);
        sm3.beginTextStream();
        sm3.receiveTextChunk(json3);
        sm3.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 11c survived styled self-reference - RISK45 did NOT hit");
    }

    /**
     * Test 12 (RISK46): updateDataModel with non-string "path" field.
     * Surface::updateDataModel() L242 does:
     *   dataModelData["path"].get<std::string>() — NO is_string() check.
     * Sending "path": 12345 (integer) triggers nlohmann::detail::type_error
     * which is uncaught → std::terminate() → SIGABRT.
     *
     * This proves the RISK40/41 vulnerability class extends to updateDataModel
     * protocol commands, not just updateComponents.
     */
    @Test(timeout = 15000)
    public void test_updateDataModelPathTypeMismatch() throws Exception {
        Log.i(TAG, "=== Test 12 (RISK46): updateDataModel path type mismatch ===");
        String surfaceId = "s_r46_t12";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // First create a valid surface with a component
        String setupJson = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"txt1\",\"component\":\"Text\",\"value\":\"hello\"}]}}";
        sm.beginTextStream();
        sm.receiveTextChunk(setupJson);
        sm.endTextStream();
        Thread.sleep(500);

        // Now send updateDataModel with path as integer instead of string
        String malformedJson = "{\"updateDataModel\":{\"surfaceId\":\"" + surfaceId
                + "\",\"path\":12345,\"value\":\"crash\"}}";

        Log.i(TAG, "Sending updateDataModel with integer path, JSON=" + malformedJson);
        sm.beginTextStream();
        sm.receiveTextChunk(malformedJson);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 12a survived integer path");

        // Variant: path as null
        String nullPathJson = "{\"updateDataModel\":{\"surfaceId\":\"" + surfaceId
                + "\",\"path\":null,\"value\":\"data\"}}";

        Log.i(TAG, "Sending updateDataModel with null path, JSON=" + nullPathJson);
        sm.beginTextStream();
        sm.receiveTextChunk(nullPathJson);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Test 12b survived null path - RISK46 did NOT hit");
    }

    /**
     * RISK47: appendDataModel path type mismatch → nlohmann::type_error → SIGABRT
     *
     * parseAppendDataModelData() has try-catch that protects surfaceId,
     * but "path" field type error escapes to Surface::appendDataModel() L263:
     *   std::string path = dataModelData.contains("path") ? dataModelData["path"].get<std::string>() : "/";
     * No try-catch wraps this call → uncaught exception → std::terminate → SIGABRT
     *
     * Also tests: surfaceId as integer in appendDataModel (parser try-catch should catch this)
     */
    @Test
    public void test_appendDataModelPathTypeMismatch() throws Exception {
        Log.i(TAG, "=== RISK47: appendDataModel path type mismatch ===");

        String surfaceId = "risk47_append_dm";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        // First: create a component so the surface is active
        String createComp = "{\"updateComponents\":{\"surfaceId\":\"" + surfaceId
                + "\",\"components\":[{\"id\":\"c1\",\"component\":\"Text\",\"attributes\":{\"content\":\"hello\"}}]}}";
        sm.beginTextStream();
        sm.receiveTextChunk(createComp);
        sm.endTextStream();
        Thread.sleep(300);

        // Variant A: appendDataModel with integer path (should crash at Surface::appendDataModel L263)
        String malformedAppend = "{\"appendDataModel\":{\"surfaceId\":\"" + surfaceId
                + "\",\"path\":99999,\"value\":\"leaked\"}}";

        Log.i(TAG, "Sending appendDataModel with integer path: " + malformedAppend);
        sm.beginTextStream();
        sm.receiveTextChunk(malformedAppend);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Variant A survived - RISK47 did NOT hit via integer path");

        // Variant B: appendDataModel with array path
        String arrayPathAppend = "{\"appendDataModel\":{\"surfaceId\":\"" + surfaceId
                + "\",\"path\":[\"a\",\"b\"],\"value\":\"data\"}}";

        Log.i(TAG, "Sending appendDataModel with array path: " + arrayPathAppend);
        sm.beginTextStream();
        sm.receiveTextChunk(arrayPathAppend);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Variant B survived - RISK47 did NOT hit via array path");

        // Variant C: appendDataModel with null path
        String nullPathAppend = "{\"appendDataModel\":{\"surfaceId\":\"" + surfaceId
                + "\",\"path\":null,\"value\":\"data\"}}";

        Log.i(TAG, "Sending appendDataModel with null path: " + nullPathAppend);
        sm.beginTextStream();
        sm.receiveTextChunk(nullPathAppend);
        sm.endTextStream();

        Thread.sleep(3000);
        Log.i(TAG, "Variant C survived - RISK47 did NOT hit via null path");
    }

    /**
     * RISK48: Memory leak probe — SurfaceManager create without destroy
     *
     * Hypothesis: Creating SurfaceManagers, feeding data, then dropping Java
     * references without calling destroy() leaks native memory (Yoga nodes,
     * C++ Surface objects, JNI global refs). After GC, native heap should not
     * grow if cleanup is correct.
     *
     * This test creates 50 SurfaceManagers, feeds each one component data,
     * then drops all references and forces GC. If native memory grows linearly,
     * it proves a leak.
     */
    @Test
    public void test_memoryLeakCreateWithoutDestroy() throws Exception {
        Log.i(TAG, "=== RISK48: Memory leak — create without destroy ===");

        Runtime runtime = Runtime.getRuntime();
        runtime.gc();
        Thread.sleep(500);
        long baselineNative = android.os.Debug.getNativeHeapAllocatedSize();
        long baselineJava = runtime.totalMemory() - runtime.freeMemory();
        Log.i(TAG, "Baseline: native=" + baselineNative + " java=" + baselineJava);

        // Phase 1: Create 50 SurfaceManagers, feed data, drop references
        for (int i = 0; i < 50; i++) {
            SurfaceManager sm = new SurfaceManager(activity);
            String sid = "leak_" + i;

            String createSurface = "{\"createSurface\":{\"surfaceId\":\"" + sid + "\",\"catalogId\":\"test\"}}";
            sm.beginTextStream();
            sm.receiveTextChunk(createSurface);
            sm.endTextStream();

            // Feed component data to allocate Yoga nodes
            String compData = "{\"updateComponents\":{\"surfaceId\":\"" + sid
                    + "\",\"components\":[{\"id\":\"c" + i + "\",\"component\":\"Text\","
                    + "\"attributes\":{\"content\":\"item" + i + "\"},"
                    + "\"style\":{\"width\":\"100px\",\"height\":\"50px\",\"padding\":\"10px\"}}]}}";
            sm.beginTextStream();
            sm.receiveTextChunk(compData);
            sm.endTextStream();

            // Do NOT call destroy() — intentional leak test
            // sm = null; // drop reference
        }

        Thread.sleep(1000);

        // Force GC multiple times
        for (int gc = 0; gc < 5; gc++) {
            runtime.gc();
            System.runFinalization();
            Thread.sleep(200);
        }
        Thread.sleep(2000);

        long afterNative = android.os.Debug.getNativeHeapAllocatedSize();
        long afterJava = runtime.totalMemory() - runtime.freeMemory();
        long nativeDelta = afterNative - baselineNative;
        long javaDelta = afterJava - baselineJava;

        Log.i(TAG, "After 50 SM without destroy: native=" + afterNative
                + " java=" + afterJava);
        Log.i(TAG, "Delta: native=+" + nativeDelta + " java=+" + javaDelta);

        // Phase 2: Create 50 more to show linear growth
        for (int i = 50; i < 100; i++) {
            SurfaceManager sm = new SurfaceManager(activity);
            String sid = "leak_" + i;

            String createSurface = "{\"createSurface\":{\"surfaceId\":\"" + sid + "\",\"catalogId\":\"test\"}}";
            sm.beginTextStream();
            sm.receiveTextChunk(createSurface);
            sm.endTextStream();

            String compData = "{\"updateComponents\":{\"surfaceId\":\"" + sid
                    + "\",\"components\":[{\"id\":\"c" + i + "\",\"component\":\"Text\","
                    + "\"attributes\":{\"content\":\"item" + i + "\"},"
                    + "\"style\":{\"width\":\"100px\",\"height\":\"50px\",\"padding\":\"10px\"}}]}}";
            sm.beginTextStream();
            sm.receiveTextChunk(compData);
            sm.endTextStream();
        }

        Thread.sleep(1000);
        for (int gc = 0; gc < 5; gc++) {
            runtime.gc();
            System.runFinalization();
            Thread.sleep(200);
        }
        Thread.sleep(2000);

        long finalNative = android.os.Debug.getNativeHeapAllocatedSize();
        long finalJava = runtime.totalMemory() - runtime.freeMemory();
        long totalNativeDelta = finalNative - baselineNative;

        Log.i(TAG, "After 100 SM without destroy: native=" + finalNative
                + " java=" + finalJava);
        Log.i(TAG, "Total native delta from baseline: +" + totalNativeDelta);

        // If native delta > 5MB for 100 surfaces, it's a significant leak
        if (totalNativeDelta > 5 * 1024 * 1024) {
            Log.e(TAG, "RISK48 HIT: Native memory grew by " + (totalNativeDelta / 1024 / 1024)
                    + "MB — likely native memory leak!");
        } else {
            Log.i(TAG, "RISK48 NOT HIT: Native memory delta within acceptable range");
        }

        Log.i(TAG, "=== RISK48 complete (check native delta in logs) ===");
    }

    /**
     * RISK49: Concurrent receiveTextChunk on same SurfaceManager → buffer corruption / crash
     *
     * R2: More aggressive — interleave begin/receive/end across threads without
     * proper pairing. One thread does raw receiveTextChunk, another does begin/end
     * cycling, creating race conditions on the internal stream state.
     */
    @Test
    public void test_concurrentReceiveTextChunk() throws Exception {
        Log.i(TAG, "=== RISK49 R2: Concurrent receiveTextChunk (aggressive) ===");

        String surfaceId = "risk49_concurrent_chunk";
        SurfaceManager sm = createSurfaceAndWait(surfaceId);
        if (sm == null) return;

        final SurfaceManager finalSm = sm;
        final String sid = surfaceId;
        final int ITERATIONS = 200;
        final AtomicBoolean crashed = new AtomicBoolean(false);

        // Thread 1: Rapid begin/end cycling
        Thread cycler = new Thread(() -> {
            try {
                for (int i = 0; i < ITERATIONS; i++) {
                    finalSm.beginTextStream();
                    Thread.sleep(0, 100); // tiny yield
                    finalSm.endTextStream();
                }
            } catch (Exception e) {
                Log.e(TAG, "Cycler exception: " + e.getMessage());
                crashed.set(true);
            }
        }, "risk49-cycler");

        // Thread 2: Raw receiveTextChunk without begin/end
        Thread injector1 = new Thread(() -> {
            try {
                for (int i = 0; i < ITERATIONS; i++) {
                    String comp = "{\"updateComponents\":{\"surfaceId\":\"" + sid
                            + "\",\"components\":[{\"id\":\"inj1_" + i
                            + "\",\"component\":\"Text\",\"attributes\":{\"content\":\"x\"}}]}}";
                    finalSm.receiveTextChunk(comp);
                }
            } catch (Exception e) {
                Log.e(TAG, "Injector1 exception: " + e.getMessage());
                crashed.set(true);
            }
        }, "risk49-inj1");

        // Thread 3: Valid begin/receive/end but overlapping with others
        Thread injector2 = new Thread(() -> {
            try {
                for (int i = 0; i < ITERATIONS; i++) {
                    String comp = "{\"updateComponents\":{\"surfaceId\":\"" + sid
                            + "\",\"components\":[{\"id\":\"inj2_" + i
                            + "\",\"component\":\"Text\",\"attributes\":{\"content\":\"y\"}}]}}";
                    finalSm.beginTextStream();
                    finalSm.receiveTextChunk(comp);
                    finalSm.endTextStream();
                }
            } catch (Exception e) {
                Log.e(TAG, "Injector2 exception: " + e.getMessage());
                crashed.set(true);
            }
        }, "risk49-inj2");

        // Thread 4: Partial chunks (split JSON across calls)
        Thread splitter = new Thread(() -> {
            try {
                for (int i = 0; i < ITERATIONS; i++) {
                    String half1 = "{\"updateComponents\":{\"surfaceId\":\"" + sid + "\",";
                    String half2 = "\"components\":[{\"id\":\"spl_" + i
                            + "\",\"component\":\"Text\",\"attributes\":{\"content\":\"z\"}}]}}";
                    finalSm.beginTextStream();
                    finalSm.receiveTextChunk(half1);
                    finalSm.receiveTextChunk(half2);
                    finalSm.endTextStream();
                }
            } catch (Exception e) {
                Log.e(TAG, "Splitter exception: " + e.getMessage());
                crashed.set(true);
            }
        }, "risk49-splitter");

        // Start all simultaneously
        cycler.start();
        injector1.start();
        injector2.start();
        splitter.start();

        cycler.join(30000);
        injector1.join(30000);
        injector2.join(30000);
        splitter.join(30000);

        Thread.sleep(2000);
        Log.i(TAG, "RISK49 R2: All threads completed, crashed=" + crashed.get());

        if (crashed.get()) {
            Log.e(TAG, "RISK49 HIT: Concurrent stream operations caused crash!");
        } else {
            Log.i(TAG, "RISK49 R2 survived aggressive concurrent chunk injection");
        }
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
