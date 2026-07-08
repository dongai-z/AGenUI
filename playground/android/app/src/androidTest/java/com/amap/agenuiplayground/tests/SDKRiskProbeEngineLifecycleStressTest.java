package com.amap.agenuiplayground.tests;

import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.ext.junit.rules.ActivityScenarioRule;

import com.amap.agenui.AGenUI;
import com.amap.agenui.render.surface.SurfaceManager;
import com.amap.agenuiplayground.A2UIPlaygroundActivity;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * RISK37: Combined engine lifecycle stress — rapid SM create/stream/destroy cycles
 * with large payloads and engine-level destroy interleaved.
 *
 * <h3>Root cause hypothesis</h3>
 *
 * ThreadManager::getMessageThread() returns a raw IThread* pointer that is
 * NOT ref-counted. AGenUIEngine::stop() calls destroyThread() which removes,
 * stops, and DELETES the shared thread. Any code holding a stale IThread*
 * between getMessageThread() returning and messageThread->post() being called
 * will crash with UAF.
 *
 * This probe amplifies the race window by:
 *   1. Using 50KB+ payloads to slow JNI string conversion (widens the timing
 *      window inside jni_receiveTextChunk between findSurfaceManagerByInstanceId
 *      and surfaceManager->receiveTextChunk)
 *   2. Using 20+ concurrent threads to maximize preemption probability
 *   3. Running multiple init/destroy cycles to increase cumulative hit probability
 *   4. Sending data immediately after SM creation (before init lambda executes)
 *      to stress shared_from_this() while the SM is in a transitional state
 *
 * Additionally tests whether the C++ worker thread, when heavily loaded with
 * pending tasks, creates a wider window during engine stop (queue drain + join).
 *
 * Probe style: timing/concurrency race + pressure (large payloads, many threads)
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeEngineLifecycleStressTest {

    private static final String TAG = "SDKRiskProbe_RISK37";
    private static final int NUM_STREAMING_THREADS = 20;
    private static final int NUM_SURFACE_MANAGERS = 10;
    private static final int NUM_CYCLES = 5;

    @Rule
    public ActivityScenarioRule<A2UIPlaygroundActivity> activityRule =
            new ActivityScenarioRule<>(A2UIPlaygroundActivity.class);

    /**
     * Generate a large JSON payload (~50KB) to slow down JNI string conversion,
     * widening the timing window for the race.
     */
    private static String generateLargePayload(String surfaceId) {
        // Build a valid updateDataModel with a large "value" object
        StringBuilder sb = new StringBuilder(55000);
        sb.append("{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"");
        sb.append(surfaceId);
        sb.append("\",\"value\":{\"data\":\"");
        // Fill with ~50KB of data
        for (int i = 0; i < 50000; i++) {
            sb.append('A');
        }
        sb.append("\"}}}");
        return sb.toString();
    }

    /**
     * Generate a medium-sized updateComponents payload.
     */
    private static String generateMediumPayload(String surfaceId) {
        StringBuilder sb = new StringBuilder(12000);
        sb.append("{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"");
        sb.append(surfaceId);
        sb.append("\",\"components\":[");
        for (int i = 0; i < 50; i++) {
            if (i > 0) sb.append(",");
            sb.append("{\"component\":\"Text\",\"id\":\"t").append(i)
              .append("\",\"attributes\":{\"value\":\"Item ").append(i)
              .append(" with some padding text to increase payload size for this test\"}}");
        }
        sb.append("]}}");
        return sb.toString();
    }

    /**
     * Test 1: High-pressure streaming with large payloads + engine destroy.
     *
     * 20 threads × 10 SMs streaming 50KB chunks in tight loops.
     * After 1 second of pressure, engine.destroy() is called.
     * The large payload size slows JNI string conversion, widening the race window
     * between findSurfaceManagerByInstanceId and surfaceManager->receiveTextChunk.
     *
     * Expected: SIGSEGV from accessing deleted MessageThread or freed SurfaceManager.
     */
    @Test(timeout = 60_000)
    public void testRISK37_largePayloadStreamingDuringDestroy() throws Exception {
        Log.i(TAG, "=== RISK37-T1: Large payload streaming + engine destroy ===");

        activityRule.getScenario().onActivity(activity -> {
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(activity.getApplicationContext());
            }
        });
        Thread.sleep(200);

        // Create many SurfaceManagers
        final SurfaceManager[] sms = new SurfaceManager[NUM_SURFACE_MANAGERS];
        activityRule.getScenario().onActivity(activity -> {
            for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
                sms[i] = new SurfaceManager(activity);
            }
        });
        Thread.sleep(500); // Allow all init lambdas to process

        // Create surfaces on each SM
        for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
            String createJson = "{\"version\":\"v0.9\",\"createSurface\":{\"surfaceId\":\"r37-"
                    + i + "\",\"catalogId\":\"test\",\"theme\":{},\"sendDataModel\":false,\"animated\":false}}";
            sms[i].beginTextStream();
            sms[i].receiveTextChunk(createJson);
            sms[i].endTextStream();
        }
        Thread.sleep(300);

        // Pre-generate large payloads (50KB each)
        final String[] payloads = new String[NUM_SURFACE_MANAGERS];
        for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
            payloads[i] = generateLargePayload("r37-" + i);
        }

        final AtomicBoolean stop = new AtomicBoolean(false);
        final CyclicBarrier startBarrier = new CyclicBarrier(NUM_STREAMING_THREADS + 1);
        final AtomicInteger totalOps = new AtomicInteger(0);

        // Launch streaming threads
        Thread[] streamers = new Thread[NUM_STREAMING_THREADS];
        for (int t = 0; t < NUM_STREAMING_THREADS; t++) {
            final int tid = t;
            final SurfaceManager sm = sms[t % NUM_SURFACE_MANAGERS];
            final String payload = payloads[t % NUM_SURFACE_MANAGERS];
            streamers[t] = new Thread(() -> {
                try { startBarrier.await(5, TimeUnit.SECONDS); } catch (Exception e) { return; }
                while (!stop.get()) {
                    try {
                        sm.beginTextStream();
                        sm.receiveTextChunk(payload);
                        sm.receiveTextChunk(payload);
                        sm.endTextStream();
                        totalOps.incrementAndGet();
                    } catch (Exception e) { /* Java exceptions are fine */ }
                }
            }, "risk37-stream-" + tid);
            streamers[t].start();
        }

        // Release all threads and let them build pressure
        startBarrier.await(5, TimeUnit.SECONDS);
        Thread.sleep(1000); // Build heavy queue pressure
        Log.i(TAG, "Warm-up done, ops: " + totalOps.get());

        // NOW: destroy engine while all threads are actively streaming large payloads
        Log.i(TAG, ">>> AGenUI.destroy() while " + NUM_STREAMING_THREADS
                + " threads stream 50KB payloads <<<");
        AGenUI.getInstance().destroy();
        Log.i(TAG, "destroy() returned");

        // Keep streaming for a bit more (to catch late races)
        Thread.sleep(500);
        stop.set(true);
        for (Thread t : streamers) t.join(5000);

        Log.i(TAG, "Total ops: " + totalOps.get());
        Log.i(TAG, "=== RISK37-T1 completed WITHOUT crash ===");
    }

    /**
     * Test 2: Rapid SM create-stream-destroy cycles + engine destroy.
     *
     * Multiple threads each run tight loops of:
     *   new SurfaceManager → beginTextStream → receiveTextChunk → destroy
     * This tests the interaction between SM init (posted to worker) and
     * immediate streaming (also posted) followed by immediate destroy.
     *
     * While these cycles run, another thread calls AGenUI.destroy().
     * The rapid create/destroy means many SMs exist briefly in the engine map,
     * maximizing the chance of a SM being freed while JNI holds a raw pointer.
     *
     * Expected: SIGSEGV from stale SM pointer or deleted worker thread.
     */
    @Test(timeout = 60_000)
    public void testRISK37_rapidCreateStreamDestroyCycle() throws Exception {
        Log.i(TAG, "=== RISK37-T2: Rapid create-stream-destroy cycles ===");

        activityRule.getScenario().onActivity(activity -> {
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(activity.getApplicationContext());
            }
        });
        Thread.sleep(200);

        final AtomicBoolean stop = new AtomicBoolean(false);
        final AtomicInteger totalCycles = new AtomicInteger(0);
        final AtomicInteger createFailures = new AtomicInteger(0);
        final int CYCLE_THREADS = 12;
        final CyclicBarrier startBarrier = new CyclicBarrier(CYCLE_THREADS + 1);

        final String smallPayload = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"cycle\",\"value\":{\"x\":1}}}";

        Thread[] cyclers = new Thread[CYCLE_THREADS];
        for (int t = 0; t < CYCLE_THREADS; t++) {
            final int tid = t;
            cyclers[t] = new Thread(() -> {
                try { startBarrier.await(5, TimeUnit.SECONDS); } catch (Exception e) { return; }
                while (!stop.get()) {
                    SurfaceManager sm = null;
                    try {
                        // Use onActivity to get activity reference on main thread
                        final SurfaceManager[] holder = new SurfaceManager[1];
                        final CountDownLatch created = new CountDownLatch(1);
                        activityRule.getScenario().onActivity(activity -> {
                            try {
                                holder[0] = new SurfaceManager(activity);
                            } catch (Exception e) {
                                createFailures.incrementAndGet();
                            }
                            created.countDown();
                        });
                        if (!created.await(2, TimeUnit.SECONDS)) continue;
                        sm = holder[0];
                        if (sm == null) continue;

                        // Immediately stream without waiting for init
                        sm.beginTextStream();
                        sm.receiveTextChunk(smallPayload);
                        sm.receiveTextChunk(smallPayload);
                        sm.endTextStream();

                        // Immediately destroy
                        sm.destroy();
                        totalCycles.incrementAndGet();
                    } catch (Exception e) {
                        // IllegalStateException from createSurfaceManager = engine gone
                        createFailures.incrementAndGet();
                    }
                }
            }, "risk37-cycle-" + tid);
            cyclers[t].start();
        }

        startBarrier.await(5, TimeUnit.SECONDS);
        Thread.sleep(2000); // Let cycles run

        Log.i(TAG, "Cycles completed: " + totalCycles.get()
                + ", create failures: " + createFailures.get());

        // Destroy engine mid-cycle
        Log.i(TAG, ">>> AGenUI.destroy() while " + CYCLE_THREADS + " threads cycle <<<");
        AGenUI.getInstance().destroy();
        Log.i(TAG, "destroy() returned");

        Thread.sleep(500);
        stop.set(true);
        for (Thread t : cyclers) t.join(5000);

        Log.i(TAG, "Final cycles: " + totalCycles.get()
                + ", create failures: " + createFailures.get());
        Log.i(TAG, "=== RISK37-T2 completed WITHOUT crash ===");
    }

    /**
     * Test 3: Multiple engine init/destroy cycles with concurrent SM operations.
     *
     * Repeatedly:
     *   1. Initialize engine
     *   2. Create SMs + start streaming on background threads
     *   3. Destroy engine (while streaming continues)
     *   4. Repeat
     *
     * Each cycle creates a fresh engine state. The cumulative probability of
     * hitting the IThread* UAF race increases with each cycle. The rapid
     * engine lifecycle transitions stress the std::call_once pattern and global
     * state management.
     *
     * Expected: SIGSEGV from stale IThread*, freed SM, or corrupted global state.
     */
    @Test(timeout = 90_000)
    public void testRISK37_repeatedInitDestroyCycles() throws Exception {
        Log.i(TAG, "=== RISK37-T3: Repeated init/destroy cycles with streaming ===");

        final AtomicInteger cycleCount = new AtomicInteger(0);
        final String largePayload = generateLargePayload("cycle-s0");

        for (int cycle = 0; cycle < NUM_CYCLES; cycle++) {
            Log.i(TAG, "--- Cycle " + (cycle + 1) + "/" + NUM_CYCLES + " ---");

            // Re-initialize engine
            activityRule.getScenario().onActivity(activity -> {
                if (!AGenUI.getInstance().isInitialized()) {
                    AGenUI.getInstance().initialize(activity.getApplicationContext());
                }
            });
            Thread.sleep(200);

            // Check if engine is actually running (call_once may prevent re-init)
            boolean canCreateSM = true;
            final SurfaceManager[] sms = new SurfaceManager[5];
            try {
                activityRule.getScenario().onActivity(activity -> {
                    for (int i = 0; i < 5; i++) {
                        sms[i] = new SurfaceManager(activity);
                    }
                });
            } catch (Exception e) {
                Log.w(TAG, "Cannot create SM (engine likely dead after call_once): " + e.getMessage());
                canCreateSM = false;
            }
            Thread.sleep(200);

            if (!canCreateSM) {
                Log.i(TAG, "Engine cannot be re-initialized (std::call_once consumed). Stopping.");
                break;
            }

            // Start streaming threads
            final AtomicBoolean stop = new AtomicBoolean(false);
            final int THREADS = 10;
            Thread[] streamers = new Thread[THREADS];
            for (int t = 0; t < THREADS; t++) {
                final int tid = t;
                final SurfaceManager sm = sms[t % 5];
                streamers[t] = new Thread(() -> {
                    while (!stop.get()) {
                        try {
                            sm.beginTextStream();
                            sm.receiveTextChunk(largePayload);
                            sm.endTextStream();
                        } catch (Exception e) { /* ignore */ }
                    }
                }, "risk37-c" + cycle + "-s" + tid);
                streamers[t].start();
            }

            // Let streaming build pressure
            Thread.sleep(800);

            // Destroy while streaming
            Log.i(TAG, "Destroying engine (cycle " + (cycle + 1) + ")...");
            AGenUI.getInstance().destroy();
            Log.i(TAG, "Engine destroyed");

            // Stop streamers
            stop.set(true);
            for (Thread t : streamers) {
                if (t != null) t.join(3000);
            }

            cycleCount.incrementAndGet();
            Thread.sleep(300); // Brief pause between cycles
        }

        Log.i(TAG, "Completed " + cycleCount.get() + " init/destroy cycles");
        Log.i(TAG, "=== RISK37-T3 completed WITHOUT crash ===");
    }
}
