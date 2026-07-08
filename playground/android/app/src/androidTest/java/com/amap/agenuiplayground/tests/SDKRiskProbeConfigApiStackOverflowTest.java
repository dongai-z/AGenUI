package com.amap.agenuiplayground.tests;

import android.app.Activity;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.rule.ActivityTestRule;

import com.amap.agenui.AGenUI;
import com.amap.agenuiplayground.A2UIPlaygroundActivity;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * RISK43: Public configuration APIs with deeply nested JSON → nlohmann::json::parse
 * recursive descent stack overflow → SIGSEGV.
 *
 * VULNERABILITY:
 * AGenUI.registerDefaultTheme() and AGenUI.setPathConfig() accept arbitrary JSON strings.
 * Internally, these call nlohmann::json::parse(jsonString, nullptr, false) which uses
 * recursive descent parsing. Deeply nested JSON structures (1000+ levels) overflow the
 * call stack before the parser can return an error, resulting in SIGSEGV.
 *
 * KEY DIFFERENCE FROM RISK32/38:
 * - RISK32/38: Deep component TREE nesting → stack overflow in Yoga layout recursion
 *   (triggered via receiveTextChunk streaming on worker thread)
 * - RISK43: Deep JSON VALUE nesting → stack overflow in nlohmann::json::parse() itself
 *   (triggered via public configuration API on MAIN THREAD - no streaming needed)
 *
 * ATTACK SURFACE:
 * - AGenUI.registerDefaultTheme(theme, designToken)  [main thread, direct API call]
 * - AGenUI.setPathConfig(configJson)                  [main thread, direct API call]
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeConfigApiStackOverflowTest {

    private static final String TAG = "RISK43_ConfigOverflow";

    @Rule
    public ActivityTestRule<A2UIPlaygroundActivity> activityRule =
            new ActivityTestRule<>(A2UIPlaygroundActivity.class);

    @Before
    public void setUp() {
        Activity activity = activityRule.getActivity();
        AGenUI.getInstance().initialize(activity.getApplicationContext());
        Log.d(TAG, "AGenUI initialized");
    }

    @After
    public void tearDown() {
        try {
            AGenUI.getInstance().destroy();
        } catch (Exception e) {
            // ignore
        }
    }

    /**
     * Helper: build deeply nested JSON object string.
     * e.g., depth=3 → {"a":{"a":{"a":"x"}}}
     */
    private String buildDeeplyNestedJson(int depth) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < depth; i++) {
            sb.append("{\"a\":");
        }
        sb.append("\"x\"");
        for (int i = 0; i < depth; i++) {
            sb.append("}");
        }
        return sb.toString();
    }

    // =========================================================================
    // Test 1: registerDefaultTheme (designToken path) with 2000-level nested JSON
    // =========================================================================
    @Test(timeout = 30000)
    public void test_designTokenConfigDeepNesting() throws Exception {
        Log.d(TAG, "Test 1: registerDefaultTheme(designToken) with 2000-level nested JSON");

        String deepValue = buildDeeplyNestedJson(2000);
        String json = "{\"designTokens\":" + deepValue + "}";

        Log.d(TAG, "Sending deeply nested config (2000 levels), length=" + json.length());

        try {
            // Second param is designToken - calls nativeLoadDesignTokenConfig → nlohmann::json::parse
            AGenUI.getInstance().registerDefaultTheme("{}", json);
        } catch (Exception e) {
            Log.d(TAG, "Exception: " + e.getMessage());
        }

        Thread.sleep(1000);
        Log.d(TAG, "Test 1 completed without crash (unexpected)");
    }

    // =========================================================================
    // Test 2: registerDefaultTheme (theme path) with 2000-level nested JSON
    // =========================================================================
    @Test(timeout = 30000)
    public void test_themeConfigDeepNesting() throws Exception {
        Log.d(TAG, "Test 2: registerDefaultTheme(theme) with 2000-level nested JSON");

        String deepValue = buildDeeplyNestedJson(2000);
        String json = "{\"theme\":" + deepValue + "}";

        Log.d(TAG, "Sending deeply nested theme config (2000 levels), length=" + json.length());

        try {
            // First param is theme - calls nativeLoadThemeConfig → nlohmann::json::parse
            AGenUI.getInstance().registerDefaultTheme(json, "{}");
        } catch (Exception e) {
            Log.d(TAG, "Exception: " + e.getMessage());
        }

        Thread.sleep(1000);
        Log.d(TAG, "Test 2 completed without crash (unexpected)");
    }

    // =========================================================================
    // Test 3: setPathConfig with 2000-level nested JSON
    // =========================================================================
    @Test(timeout = 30000)
    public void test_setPathConfigDeepNesting() throws Exception {
        Log.d(TAG, "Test 3: setPathConfig with 2000-level nested JSON");

        String deepValue = buildDeeplyNestedJson(2000);
        String configJson = "{\"paths\":" + deepValue + "}";

        Log.d(TAG, "Sending deeply nested path config (2000 levels), length=" + configJson.length());

        try {
            AGenUI.getInstance().setPathConfig(configJson);
        } catch (Exception e) {
            Log.d(TAG, "Exception: " + e.getMessage());
        }

        Thread.sleep(1000);
        Log.d(TAG, "Test 3 completed without crash (unexpected)");
    }

    // =========================================================================
    // Test 4: Escalation - 5000-level nesting (higher pressure)
    // =========================================================================
    @Test(timeout = 30000)
    public void test_designTokenConfig5000Levels() throws Exception {
        Log.d(TAG, "Test 4: registerDefaultTheme with 5000-level nested JSON");

        String deepValue = buildDeeplyNestedJson(5000);
        String json = "{\"designTokens\":" + deepValue + "}";

        Log.d(TAG, "Sending deeply nested config (5000 levels), length=" + json.length());

        try {
            AGenUI.getInstance().registerDefaultTheme("{}", json);
        } catch (Exception e) {
            Log.d(TAG, "Exception: " + e.getMessage());
        }

        Thread.sleep(1000);
        Log.d(TAG, "Test 4 completed without crash (unexpected)");
    }
}
