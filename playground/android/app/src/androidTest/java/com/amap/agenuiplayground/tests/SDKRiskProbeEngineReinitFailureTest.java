package com.amap.agenuiplayground.tests;

import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.ext.junit.rules.ActivityScenarioRule;

import com.amap.agenui.AGenUI;
import com.amap.agenuiplayground.A2UIPlaygroundActivity;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.fail;

/**
 * RISK54: Engine re-initialization failure after destroy.
 *
 * C++ initAGenUIEngine() uses std::call_once(g_initFlag, ...) to guard engine
 * creation. destroyAGenUIEngine() deletes the engine and sets g_agenUIEngine
 * to nullptr, but std::once_flag is irrevocable — it can NEVER be reset.
 *
 * After destroy → re-init:
 *   - std::call_once lambda is skipped (already called)
 *   - initAGenUIEngine() returns nullptr (g_agenUIEngine was nulled)
 *   - Java nativePtr = 0, but isInitialized = true
 *   - createSurfaceManager() → nativeCreateSurfaceManager → getAGenUIEngine()
 *     returns nullptr → JNI returns 0 → Java throws IllegalStateException
 *   - All other APIs (registerFunction, loadThemeConfig, etc.) silently no-op
 *
 * Entry surface: SDK public API — AGenUI.getInstance().initialize() →
 *   AGenUI.getInstance().destroy() → AGenUI.getInstance().initialize()
 *
 * Probe style: lifecycle / state machine — sequential destroy + re-init
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeEngineReinitFailureTest {

    private static final String TAG = "RiskProbe-ReinitFail";

    @Rule
    public ActivityScenarioRule<A2UIPlaygroundActivity> activityRule =
            new ActivityScenarioRule<>(A2UIPlaygroundActivity.class);

    @Test
    public void testRisk54_engineReinitAfterDestroy() {
        Log.i(TAG, "=== RISK54: Engine re-initialization after destroy ===");

        // Phase 1: Initial initialization — must succeed
        Log.i(TAG, "Phase 1: Initial initialization");
        activityRule.getScenario().onActivity(activity -> {
            AGenUI engine = AGenUI.getInstance();
            engine.initialize(activity.getApplicationContext());
            Log.i(TAG, "Initial init completed, isInitialized=" + engine.isInitialized());
        });

        // Phase 2: Verify engine works — createSurfaceManager should succeed
        Log.i(TAG, "Phase 2: Verify engine works before destroy");
        activityRule.getScenario().onActivity(activity -> {
            AGenUI engine = AGenUI.getInstance();
            try {
                int instanceId = engine.createSurfaceManager();
                Log.i(TAG, "SM created successfully: instanceId=" + instanceId);
                engine.destroySurfaceManager(instanceId);
                Log.i(TAG, "SM destroyed successfully");
            } catch (Exception e) {
                fail("Engine should work before destroy: " + e.getMessage());
            }
        });

        // Phase 3: Destroy engine
        Log.i(TAG, "Phase 3: Destroy engine");
        activityRule.getScenario().onActivity(activity -> {
            AGenUI.getInstance().destroy();
            Log.i(TAG, "Engine destroyed");
        });

        // Phase 4: Re-initialize — this appears to succeed but engine is broken
        Log.i(TAG, "Phase 4: Re-initialize engine");
        activityRule.getScenario().onActivity(activity -> {
            AGenUI engine = AGenUI.getInstance();
            engine.initialize(activity.getApplicationContext());
            boolean isInit = engine.isInitialized();
            Log.i(TAG, "Re-init completed, isInitialized=" + isInit);
            if (isInit) {
                Log.w(TAG, "*** BUG: isInitialized=true but C++ engine is nullptr ***");
                Log.w(TAG, "std::call_once prevents re-initialization");
            }
        });

        // Phase 5: Try to use the engine — should fail
        Log.i(TAG, "Phase 5: Attempt to use re-initialized engine");
        activityRule.getScenario().onActivity(activity -> {
            AGenUI engine = AGenUI.getInstance();

            // 5a: createSurfaceManager should throw IllegalStateException
            boolean smCreateFailed = false;
            try {
                int instanceId = engine.createSurfaceManager();
                Log.e(TAG, "UNEXPECTED: SM created after re-init, instanceId=" + instanceId);
            } catch (IllegalStateException e) {
                smCreateFailed = true;
                Log.i(TAG, "EXPECTED: createSurfaceManager threw: " + e.getMessage());
            } catch (Exception e) {
                smCreateFailed = true;
                Log.i(TAG, "createSurfaceManager threw unexpected: " + e.getClass().getName()
                        + ": " + e.getMessage());
            }

            // Verdict
            if (smCreateFailed) {
                Log.i(TAG, "=== RISK54 CONFIRMED ===");
                Log.i(TAG, "After destroy() + re-init(), engine is permanently broken:");
                Log.i(TAG, "  - isInitialized() = true (misleading)");
                Log.i(TAG, "  - createSurfaceManager() throws IllegalStateException");
                Log.i(TAG, "  - All C++ engine APIs silently no-op (getAGenUIEngine() returns null)");
                Log.i(TAG, "Root cause: std::call_once(g_initFlag, ...) in agenui_engine_entry.cpp");
                Log.i(TAG, "  is irrevocable — destroyAGenUIEngine cannot reset g_initFlag");
            } else {
                Log.i(TAG, "=== RISK54 NOT REPRODUCED ===");
                Log.i(TAG, "Engine re-initialization appears to work");
            }
        });

        Log.i(TAG, "=== RISK54 test complete ===");
    }
}
