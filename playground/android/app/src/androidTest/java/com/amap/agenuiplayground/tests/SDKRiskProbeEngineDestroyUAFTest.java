package com.amap.agenuiplayground.tests;

import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.rules.ActivityScenarioRule;

import com.amap.agenui.AGenUI;
import com.amap.agenui.render.surface.SurfaceManager;
import com.amap.agenuiplayground.A2UIPlaygroundActivity;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * RISK34: AGenUI.destroy() concurrent with active SM operations causes
 * use-after-free via stale MessageThread pointer.
 *
 * <h3>Root cause analysis</h3>
 *
 * JNI layer:
 *   findSurfaceManagerByInstanceId(id) → getAGenUIEngine() → engine->findSurfaceManager(id)
 *   returns raw ISurfaceManager* pointer.
 *
 * SurfaceManager::receiveTextChunk():
 *   IThread* messageThread = getMessageThread(); // raw pointer from ThreadManager
 *   messageThread->post(...);                    // uses raw pointer
 *
 * AGenUIEngine::stop() (called by destroyAGenUIEngine):
 *   1. _isRunning = false
 *   2. ThreadManager::destroyThread(SHARED_THREAD_ID)
 *      → erases from map → thread->stop() → delete thread
 *   3. Uninit all SurfaceManagers
 *   4. Delete engine modules
 *
 * Race window:
 *   Thread A: getMessageThread() → gets valid MessageThread* raw pointer
 *   Thread B: destroyAGenUIEngine() → destroyThread() → delete thread
 *   Thread A: messageThread->post(...) → USE-AFTER-FREE → SIGSEGV
 *
 * This differs from RISK28 (SM-level destroy): this is ENGINE-level destroy
 * that kills the shared worker thread used by ALL SurfaceManagers. Any thread
 * holding a stale MessageThread pointer will crash.
 *
 * Realistic scenario: App shutdown (UI thread calls AGenUI.destroy() in
 * Activity.onDestroy) while network callbacks still deliver streaming data
 * to SurfaceManagers on background threads.
 *
 * Probe style: timing/concurrency race + stress (many threads, many SMs)
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeEngineDestroyUAFTest {

    private static final String TAG = "SDKRiskProbe_RISK34";
    private static final int NUM_STREAMING_THREADS = 8;
    private static final int NUM_SURFACE_MANAGERS = 4;

    @Rule
    public ActivityScenarioRule<A2UIPlaygroundActivity> activityRule =
            new ActivityScenarioRule<>(A2UIPlaygroundActivity.class);

    /**
     * Test: concurrent engine destroy while multiple threads are actively
     * streaming data to multiple SurfaceManagers.
     *
     * Expected: SIGSEGV (use-after-free on MessageThread pointer)
     * If test completes without crash: race was not hit in this run.
     */
    @Test
    public void testRISK34_engineDestroyDuringActiveStreaming() throws Exception {
        Log.i(TAG, "=== RISK34: Engine destroy during active streaming ===");

        // Ensure engine is initialized
        activityRule.getScenario().onActivity(activity -> {
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(activity.getApplicationContext());
            }
        });
        Thread.sleep(200);

        // Create multiple SurfaceManagers
        final SurfaceManager[] sms = new SurfaceManager[NUM_SURFACE_MANAGERS];
        activityRule.getScenario().onActivity(activity -> {
            for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
                sms[i] = new SurfaceManager(activity);
            }
        });
        Thread.sleep(300); // Allow SM init on worker thread

        // Create surfaces on each SM
        for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
            final int idx = i;
            String createJson = "{\"version\":\"v0.9\",\"createSurface\":{\"surfaceId\":\"risk34-"
                    + i + "\",\"catalogId\":\"test\",\"theme\":{},\"sendDataModel\":false,\"animated\":false}}";
            sms[i].beginTextStream();
            sms[i].receiveTextChunk(createJson);
            sms[i].endTextStream();
        }
        Thread.sleep(300); // Allow surface creation

        Log.i(TAG, "Setup complete: " + NUM_SURFACE_MANAGERS + " SMs with surfaces");

        // Prepare streaming payload
        final String updatePayload = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"risk34-0\",\"value\":{\"counter\":1}}}";

        // Synchronization: all streamers start simultaneously
        final CyclicBarrier startBarrier = new CyclicBarrier(NUM_STREAMING_THREADS + 1);
        final AtomicBoolean stop = new AtomicBoolean(false);
        final AtomicInteger totalOps = new AtomicInteger(0);

        // Launch streaming threads — each does tight loops of begin/receive/end
        Thread[] streamers = new Thread[NUM_STREAMING_THREADS];
        for (int t = 0; t < NUM_STREAMING_THREADS; t++) {
            final int threadId = t;
            final SurfaceManager targetSm = sms[t % NUM_SURFACE_MANAGERS];
            streamers[t] = new Thread(() -> {
                try {
                    startBarrier.await(5, TimeUnit.SECONDS);
                } catch (Exception e) {
                    return;
                }
                Log.i(TAG, "Streamer-" + threadId + " started on thread: "
                        + Thread.currentThread().getName());

                while (!stop.get()) {
                    try {
                        // Tight streaming loop — maximize time spent inside JNI
                        targetSm.beginTextStream();
                        targetSm.receiveTextChunk(updatePayload);
                        targetSm.receiveTextChunk(updatePayload);
                        targetSm.receiveTextChunk(updatePayload);
                        targetSm.endTextStream();
                        totalOps.incrementAndGet();
                    } catch (Exception e) {
                        // Java exceptions are fine — we're looking for native crash
                    }
                }
                Log.i(TAG, "Streamer-" + threadId + " stopped");
            }, "risk34-streamer-" + threadId);
            streamers[t].start();
        }

        // Main thread joins the barrier, releasing all streamers
        startBarrier.await(5, TimeUnit.SECONDS);

        // Let streaming run for a short while to build up pressure
        Thread.sleep(200);
        Log.i(TAG, "Streaming warm-up done, ops so far: " + totalOps.get());

        // NOW: destroy the engine while all streamers are actively using SMs
        // This is the critical moment — destroyAGenUIEngine() will:
        //   1. Null the global engine pointer (atomic)
        //   2. destroyThread(1) → delete the shared MessageThread
        //   3. Uninit all SMs
        //   4. Delete the engine
        // Any streamer thread that holds a stale MessageThread* pointer will crash.
        Log.i(TAG, ">>> Calling AGenUI.destroy() while " + NUM_STREAMING_THREADS
                + " threads are streaming <<<");
        AGenUI.getInstance().destroy();
        Log.i(TAG, "AGenUI.destroy() returned");

        // If we reach here, the race was not hit. Stop streamers gracefully.
        stop.set(true);
        for (Thread t : streamers) {
            t.join(5000);
        }

        Log.i(TAG, "All streamers joined. Total ops: " + totalOps.get());
        Log.i(TAG, "=== RISK34 completed WITHOUT crash (race not hit this run) ===");
    }

    /**
     * Variant: Adds invalidateFunctionCallValues to increase contention
     * on the shared worker thread, widening the race window.
     */
    @Test
    public void testRISK34_engineDestroyWithInvalidation() throws Exception {
        Log.i(TAG, "=== RISK34-v2: Engine destroy with invalidation pressure ===");

        activityRule.getScenario().onActivity(activity -> {
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(activity.getApplicationContext());
            }
        });
        Thread.sleep(200);

        final SurfaceManager[] sms = new SurfaceManager[NUM_SURFACE_MANAGERS];
        activityRule.getScenario().onActivity(activity -> {
            for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
                sms[i] = new SurfaceManager(activity);
            }
        });
        Thread.sleep(300);

        // Create surfaces
        for (int i = 0; i < NUM_SURFACE_MANAGERS; i++) {
            String createJson = "{\"version\":\"v0.9\",\"createSurface\":{\"surfaceId\":\"risk34v2-"
                    + i + "\",\"catalogId\":\"test\",\"theme\":{},\"sendDataModel\":false,\"animated\":false}}";
            sms[i].beginTextStream();
            sms[i].receiveTextChunk(createJson);
            sms[i].endTextStream();
        }
        Thread.sleep(300);

        final AtomicBoolean stop = new AtomicBoolean(false);
        final CyclicBarrier startBarrier = new CyclicBarrier(NUM_STREAMING_THREADS + 2);
        final AtomicInteger totalOps = new AtomicInteger(0);

        final String payload = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"risk34v2-0\",\"value\":{\"x\":1}}}";

        // Streaming threads
        Thread[] streamers = new Thread[NUM_STREAMING_THREADS];
        for (int t = 0; t < NUM_STREAMING_THREADS; t++) {
            final int tid = t;
            final SurfaceManager sm = sms[t % NUM_SURFACE_MANAGERS];
            streamers[t] = new Thread(() -> {
                try { startBarrier.await(5, TimeUnit.SECONDS); } catch (Exception e) { return; }
                while (!stop.get()) {
                    try {
                        sm.beginTextStream();
                        sm.receiveTextChunk(payload);
                        sm.endTextStream();
                        totalOps.incrementAndGet();
                    } catch (Exception e) { /* ignore */ }
                }
            }, "risk34v2-stream-" + tid);
            streamers[t].start();
        }

        // Invalidation thread — increases worker thread pressure
        Thread invalidator = new Thread(() -> {
            try { startBarrier.await(5, TimeUnit.SECONDS); } catch (Exception e) { return; }
            while (!stop.get()) {
                try {
                    for (SurfaceManager sm : sms) {
                        if (sm != null) sm.invalidateFunctionCallValues();
                    }
                } catch (Exception e) { /* ignore */ }
            }
        }, "risk34v2-invalidator");
        invalidator.start();

        // Release all threads
        startBarrier.await(5, TimeUnit.SECONDS);
        Thread.sleep(150);
        Log.i(TAG, "Warm-up done, ops: " + totalOps.get());

        // Destroy engine under maximum contention
        Log.i(TAG, ">>> Calling AGenUI.destroy() under contention <<<");
        AGenUI.getInstance().destroy();
        Log.i(TAG, "destroy() returned");

        stop.set(true);
        invalidator.join(5000);
        for (Thread t : streamers) t.join(5000);

        Log.i(TAG, "Total ops: " + totalOps.get());
        Log.i(TAG, "=== RISK34-v2 completed WITHOUT crash ===");
    }
}
