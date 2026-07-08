package com.amap.agenuiplayground.tests;

import android.app.Activity;
import android.util.Log;

import androidx.test.ext.junit.rules.ActivityScenarioRule;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.amap.agenui.AGenUI;
import com.amap.agenui.function.FunctionCallContext;
import com.amap.agenui.function.FunctionConfig;
import com.amap.agenui.function.FunctionResult;
import com.amap.agenui.function.IFunction;
import com.amap.agenui.render.surface.SurfaceManager;
import com.amap.agenuiplayground.A2UIPlaygroundActivity;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.assertTrue;

/**
 * RISK29: registerFunction / unregisterFunction concurrent race → dangling IPlatformFunction* in
 * FunctionCallManager → UAF crash when the worker thread evaluates the function call.
 *
 * <h3>Root Cause</h3>
 * {@code jni_unregisterFunction} does two non-atomic steps:
 * <ol>
 *   <li>engine->unregisterFunction(name) — removes entry from FunctionCallManager (under FCM._mutex)</li>
 *   <li>delete sPlatformFunctions[name] — deletes the AndroidPlatformFunction (under sPlatformFunctionsMutex)</li>
 * </ol>
 *
 * If another thread calls {@code jni_registerFunction} for the SAME name between steps 1 and 2:
 * <ul>
 *   <li>Thread B registers funcB in FunctionCallManager and in sPlatformFunctions</li>
 *   <li>Thread A's step 2 finds funcB in sPlatformFunctions and DELETES it</li>
 *   <li>FunctionCallManager now holds a dangling pointer to the deleted funcB</li>
 * </ul>
 *
 * When the worker thread evaluates the function (via invalidateFunctionCallValues), it calls
 * through the dangling pointer → SIGSEGV.
 *
 * <h3>Triggering Strategy</h3>
 * <ol>
 *   <li>Register "riskFunc", stream a surface using {@code ${riskFunc()}} in a text attribute</li>
 *   <li>Launch N threads that rapidly unregister+register "riskFunc"</li>
 *   <li>Concurrently call invalidateFunctionCallValues() to force re-evaluation on the worker thread</li>
 *   <li>The race creates a dangling pointer; the invalidation calls through it → crash</li>
 * </ol>
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeFuncRegUnregRaceTest {

    private static final String TAG = "RISK29";
    private static final long TIMEOUT_MS = 30_000;
    private static final String FUNC_NAME = "riskFunc";

    @Rule
    public ActivityScenarioRule<A2UIPlaygroundActivity> activityRule =
            new ActivityScenarioRule<>(A2UIPlaygroundActivity.class);

    private Activity activity;

    @Before
    public void setUp() throws Exception {
        CountDownLatch latch = new CountDownLatch(1);
        activityRule.getScenario().onActivity(a -> {
            activity = a;
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(a.getApplicationContext());
            }
            latch.countDown();
        });
        assertTrue("Activity setup timed out", latch.await(TIMEOUT_MS, TimeUnit.MILLISECONDS));
    }

    @After
    public void tearDown() {
        // Best-effort cleanup
        try {
            AGenUI.getInstance().unregisterFunction(FUNC_NAME);
        } catch (Throwable ignored) {}
    }

    // ========================================================================
    // RISK29: Concurrent register/unregister → dangling function pointer → UAF
    // ========================================================================

    @Test
    public void testRISK29_concurrentRegUnregWithInvalidation() throws Exception {
        final int NUM_UNREG_THREADS = 4;
        final int NUM_REG_THREADS = 4;
        final int NUM_INVALIDATORS = 3;
        final int ITERATIONS = 2000;

        Log.i(TAG, "=== RISK29: concurrent registerFunction/unregisterFunction + invalidation ===");

        // Step 1: Initial registration
        registerFunc();

        // Step 2: Create SM and stream a surface that uses riskFunc()
        final SurfaceManager[] smHolder = new SurfaceManager[1];
        CountDownLatch smLatch = new CountDownLatch(1);
        activityRule.getScenario().onActivity(a -> {
            smHolder[0] = new SurfaceManager(a);
            smLatch.countDown();
        });
        assertTrue("SM creation timed out", smLatch.await(TIMEOUT_MS, TimeUnit.MILLISECONDS));
        final SurfaceManager sm = smHolder[0];
        Thread.sleep(200); // Wait for native SM init

        // Stream content that uses ${riskFunc()} in a text attribute
        String json = buildFunctionCallSurfaceJson("risk29-surf");
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();
        Thread.sleep(500); // Wait for surface creation + initial function evaluation

        Log.i(TAG, "Surface created with FunctionCall attribute. Starting race...");

        // Step 3: Separate unregister-only and register-only threads to maximize dangling window
        AtomicBoolean stop = new AtomicBoolean(false);
        AtomicInteger ops = new AtomicInteger(0);
        int totalThreads = NUM_UNREG_THREADS + NUM_REG_THREADS + NUM_INVALIDATORS;
        CyclicBarrier barrier = new CyclicBarrier(totalThreads);

        // Pure unregister threads
        for (int i = 0; i < NUM_UNREG_THREADS; i++) {
            final int idx = i;
            new Thread(() -> {
                try {
                    barrier.await();
                    for (int iter = 0; iter < ITERATIONS && !stop.get(); iter++) {
                        try {
                            AGenUI.getInstance().unregisterFunction(FUNC_NAME);
                            ops.incrementAndGet();
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "Unreg-" + idx + " error", e);
                }
            }, "unreg-" + i).start();
        }

        // Pure register threads
        for (int i = 0; i < NUM_REG_THREADS; i++) {
            final int idx = i;
            new Thread(() -> {
                try {
                    barrier.await();
                    for (int iter = 0; iter < ITERATIONS && !stop.get(); iter++) {
                        try {
                            registerFunc();
                            ops.incrementAndGet();
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "Reg-" + idx + " error", e);
                }
            }, "reg-" + i).start();
        }

        // Invalidation threads: continuous re-evaluation pressure
        for (int i = 0; i < NUM_INVALIDATORS; i++) {
            final int idx = i;
            new Thread(() -> {
                try {
                    barrier.await();
                    for (int iter = 0; iter < ITERATIONS * 3 && !stop.get(); iter++) {
                        try {
                            sm.invalidateFunctionCallValues();
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "Invalidator-" + idx + " error", e);
                }
            }, "invalidator-" + i).start();
        }

        barrier.await();
        Log.i(TAG, "All " + totalThreads + " threads started. Waiting for completion...");

        // Wait for threads to finish
        Thread.sleep(8000);
        stop.set(true);
        Thread.sleep(1000);

        Log.i(TAG, "Race finished: ops=" + ops.get());

        // Cleanup
        try { sm.destroy(); } catch (Throwable ignored) {}

        Log.i(TAG, "=== RISK29 test completed (no crash = race window missed) ===");
    }

    // ========================================================================
    // RISK29b: Higher contention variant with setDayNightMode alternation
    // ========================================================================

    @Test
    public void testRISK29b_dayNightModeToggleDuringRegUnreg() throws Exception {
        final int NUM_RACERS = 4;
        final int ITERATIONS = 400;

        Log.i(TAG, "=== RISK29b: setDayNightMode toggle + concurrent reg/unreg ===");

        registerFunc();

        // Create SM and stream surface
        final SurfaceManager[] smHolder = new SurfaceManager[1];
        CountDownLatch smLatch = new CountDownLatch(1);
        activityRule.getScenario().onActivity(a -> {
            smHolder[0] = new SurfaceManager(a);
            smLatch.countDown();
        });
        assertTrue("SM creation timed out", smLatch.await(TIMEOUT_MS, TimeUnit.MILLISECONDS));
        final SurfaceManager sm = smHolder[0];
        Thread.sleep(200);

        String json = buildFunctionCallSurfaceJson("risk29b-surf");
        sm.beginTextStream();
        sm.receiveTextChunk(json);
        sm.endTextStream();
        Thread.sleep(500);

        Log.i(TAG, "Surface created. Starting day/night + reg/unreg race...");

        AtomicBoolean stop = new AtomicBoolean(false);
        CyclicBarrier barrier = new CyclicBarrier(NUM_RACERS + 1);

        // Register/unregister racers
        for (int i = 0; i < NUM_RACERS; i++) {
            final int idx = i;
            new Thread(() -> {
                try {
                    barrier.await();
                    for (int iter = 0; iter < ITERATIONS && !stop.get(); iter++) {
                        try {
                            AGenUI.getInstance().unregisterFunction(FUNC_NAME);
                            registerFunc();
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "Racer-" + idx + " error", e);
                }
            }, "racer-" + i).start();
        }

        // Day/night toggler: triggers invalidateFunctionCallValues via engine path
        new Thread(() -> {
            try {
                barrier.await();
                for (int iter = 0; iter < ITERATIONS && !stop.get(); iter++) {
                    try {
                        AGenUI.getInstance().setDayNightMode(iter % 2 == 0 ? "dark" : "light");
                    } catch (Throwable ignored) {}
                }
            } catch (Throwable e) {
                Log.e(TAG, "DayNight toggler error", e);
            }
        }, "daynight-toggler").start();

        barrier.await();
        Log.i(TAG, "All threads started. Waiting...");

        Thread.sleep(ITERATIONS * 3L);
        stop.set(true);
        Thread.sleep(1000);

        try { sm.destroy(); } catch (Throwable ignored) {}
        Log.i(TAG, "=== RISK29b completed ===");
    }

    // ========================================================================
    // RISK29c: Multiple SMs with function call, higher parallelism
    // ========================================================================

    @Test
    public void testRISK29c_multipleSMsWithFuncCallRace() throws Exception {
        final int NUM_SMS = 3;
        final int NUM_RACERS = 6;
        final int ITERATIONS = 300;

        Log.i(TAG, "=== RISK29c: multiple SMs + func call race ===");

        registerFunc();

        // Create multiple SMs each with a surface using the function
        final SurfaceManager[] sms = new SurfaceManager[NUM_SMS];
        for (int i = 0; i < NUM_SMS; i++) {
            final int idx = i;
            CountDownLatch smLatch = new CountDownLatch(1);
            activityRule.getScenario().onActivity(a -> {
                sms[idx] = new SurfaceManager(a);
                smLatch.countDown();
            });
            assertTrue("SM " + i + " creation timed out", smLatch.await(TIMEOUT_MS, TimeUnit.MILLISECONDS));
        }
        Thread.sleep(200);

        // Stream surfaces
        for (int i = 0; i < NUM_SMS; i++) {
            String json = buildFunctionCallSurfaceJson("risk29c-surf-" + i);
            sms[i].beginTextStream();
            sms[i].receiveTextChunk(json);
            sms[i].endTextStream();
        }
        Thread.sleep(500);

        Log.i(TAG, NUM_SMS + " surfaces created. Starting race...");

        AtomicBoolean stop = new AtomicBoolean(false);
        CyclicBarrier barrier = new CyclicBarrier(NUM_RACERS + NUM_SMS);

        // Register/unregister racers
        for (int i = 0; i < NUM_RACERS; i++) {
            final int idx = i;
            new Thread(() -> {
                try {
                    barrier.await();
                    for (int iter = 0; iter < ITERATIONS && !stop.get(); iter++) {
                        try {
                            AGenUI.getInstance().unregisterFunction(FUNC_NAME);
                            registerFunc();
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "Racer-" + idx + " error", e);
                }
            }, "racer-" + i).start();
        }

        // Each SM invalidates its function calls
        for (int i = 0; i < NUM_SMS; i++) {
            final SurfaceManager sm = sms[i];
            final int idx = i;
            new Thread(() -> {
                try {
                    barrier.await();
                    for (int iter = 0; iter < ITERATIONS && !stop.get(); iter++) {
                        try {
                            sm.invalidateFunctionCallValues();
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "Invalidator-" + idx + " error", e);
                }
            }, "invalidator-" + i).start();
        }

        barrier.await();
        Log.i(TAG, "All threads started. Waiting...");

        Thread.sleep(ITERATIONS * 3L);
        stop.set(true);
        Thread.sleep(1000);

        // Cleanup
        for (SurfaceManager sm : sms) {
            try { sm.destroy(); } catch (Throwable ignored) {}
        }
        Log.i(TAG, "=== RISK29c completed ===");
    }

    // ==================== Helpers ====================

    private void registerFunc() {
        AGenUI.getInstance().registerFunction(new IFunction() {
            @Override
            public FunctionResult execute(FunctionCallContext context, String jsonString) {
                return FunctionResult.createSuccess("risk29-ok");
            }

            @Override
            public FunctionConfig getConfig() {
                return new FunctionConfig(FUNC_NAME);
            }
        });
    }

    /**
     * Builds A2UI JSON that creates a surface with a Text component whose text attribute
     * is a FunctionCall expression: ${riskFunc()}. This ensures the worker thread evaluates
     * the function during rendering and invalidation.
     */
    private String buildFunctionCallSurfaceJson(String surfaceId) {
        return "{\"version\":\"v0.9\","
                + "\"createSurface\":{\"surfaceId\":\"" + surfaceId + "\","
                + "\"catalogId\":\"test\","
                + "\"updateComponents\":["
                + "{\"id\":\"root\",\"component\":\"Column\",\"children\":[\"fc-text\"]},"
                + "{\"id\":\"fc-text\",\"component\":\"Text\","
                + "\"attributes\":{\"text\":\"${" + FUNC_NAME + "()}\"}}"
                + "]}}";
    }
}
