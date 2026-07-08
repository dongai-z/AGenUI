package com.amap.agenuiplayground.tests;

import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.amap.agenui.AGenUI;
import com.amap.agenui.render.surface.SurfaceManager;
import com.amap.agenuiplayground.base.AGenUIBaseTest;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * RISK24 / RISK25: SurfaceCoordinator concurrent access — data race between
 * receiveTextChunk (caller thread) and worker-thread operations.
 *
 * <h3>Root cause analysis</h3>
 *
 * {@code SurfaceCoordinator} has <b>no internal mutex</b>. Its internal state
 * ({@code std::map<string, unique_ptr<Surface>> _surfaces}, component trees,
 * {@code BatchGuard} batch depth counters) are all designed for single-threaded
 * access. However, two unsynchronized threads access them concurrently:
 *
 * <ul>
 *   <li><b>Thread A</b> (caller of {@code receiveTextChunk}):
 *       {@code receiveTextChunk} → {@code StreamingContentParser::processDataAssembling}
 *       → {@code dispatchParseResultsBatched} → {@code SurfaceCoordinator::createSurface
 *       / updateComponents}. This runs <b>directly on the caller thread</b> (no post).
 *       Modifies {@code _surfaces} (inserts), Surface component trees, BatchGuard state.</li>
 *
 *   <li><b>Thread B</b> (worker thread, via posted tasks):
 *       {@code setDayNightMode} → {@code engine->invalidateFunctionCallValues()} → posts
 *       to worker → {@code SurfaceCoordinator::invalidateFunctionCallValues}
 *       → {@code for (auto& pair : _surfaces) pair.second->invalidateFunctionCallValues()}.
 *       Reads {@code _surfaces}, reads/modifies Surface component state.
 *       Also: {@code handleRenderFinish}, {@code handleSurfaceSizeChanged} → find + access
 *       surfaces.</li>
 * </ul>
 *
 * Concurrent {@code std::map} insert + iterate = undefined behavior (red-black tree
 * corruption). Concurrent Surface state read + write = data race → SIGSEGV / heap
 * corruption.
 *
 * <h3>Code paths (C++)</h3>
 * <pre>
 * core/src/surface/agenui_surface_coordinator.h:71 → std::map _surfaces (NO mutex)
 * core/src/surface/agenui_surface_coordinator.cpp:42 → invalidateFunctionCallValues: iterates _surfaces
 * core/src/surface/agenui_surface_coordinator.cpp:50 → createSurface: _surfaces.emplace()
 * core/src/module/agenui_surface_manager.cpp:154 → receiveTextChunk: calls processDataAssembling DIRECTLY
 * core/src/module/agenui_surface_manager.cpp:231 → invalidateFunctionCallValues: posts to worker
 * core/src/agenui_batch_guard.h:135 → BatchGuard::_batchDepth: plain int (not atomic, no lock)
 * </pre>
 *
 * Probe style: timing/concurrency race
 */
@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeConcurrentCoordinatorTest extends AGenUIBaseTest {

    private static final String TAG = "RiskProbe-RISK24";

    @Override
    public void setUp() {
        // Only initialize AGenUI; we manage SMs ourselves
        activityRule.getScenario().onActivity(activity -> {
            if (!AGenUI.getInstance().isInitialized()) {
                AGenUI.getInstance().initialize(activity.getApplicationContext());
            }
        });
    }

    @Override
    public void tearDown() {
        // No-op: each test manages its own SM lifecycle
    }

    // ----------------------------------------------------------------
    // RISK24: concurrent createSurface (via receiveTextChunk on test thread)
    //         + invalidateFunctionCallValues (via setDayNightMode → worker thread)
    //
    // One thread streams createSurface envelopes into the SM.
    // Another thread rapidly toggles setDayNightMode, causing
    // SurfaceCoordinator::invalidateFunctionCallValues to iterate _surfaces
    // on the worker thread while _surfaces is being mutated on the test thread.
    //
    // Expected: SIGSEGV from corrupted std::map (red-black tree corruption).
    // ----------------------------------------------------------------
    @Test
    public void testRISK24_concurrentCreateSurfaceAndDayNightMode() throws Exception {
        final int ROUNDS = 15;
        final int SURFACES_PER_ROUND = 100;
        final int DAYNIGHT_PER_ROUND = 500;

        Log.i(TAG, "=== RISK24: concurrent createSurface + setDayNightMode ===");

        for (int round = 0; round < ROUNDS; round++) {
            final int r = round;
            final SurfaceManager[] smHolder = new SurfaceManager[1];
            runOnActivity(activity -> smHolder[0] = new SurfaceManager(activity));
            final SurfaceManager sm = smHolder[0];
            Thread.sleep(100); // Wait for SM init on worker thread

            CyclicBarrier barrier = new CyclicBarrier(2);
            AtomicInteger surfaces = new AtomicInteger(0);
            AtomicInteger toggles = new AtomicInteger(0);

            // Thread 1: rapid createSurface via streaming
            Thread streamer = new Thread(() -> {
                try {
                    barrier.await();
                    for (int i = 0; i < SURFACES_PER_ROUND; i++) {
                        String json = "{\"version\":\"v0.9\",\"createSurface\":"
                                + "{\"surfaceId\":\"r24-" + r + "-" + i + "\","
                                + "\"catalogId\":\"test\"}}";
                        sm.beginTextStream();
                        sm.receiveTextChunk(json);
                        sm.endTextStream();
                        surfaces.incrementAndGet();
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[RISK24] Streamer exception round=" + r, e);
                }
            }, "risk24-streamer-" + round);

            // Thread 2: rapid setDayNightMode toggling
            Thread toggler = new Thread(() -> {
                try {
                    barrier.await();
                    AGenUI sdk = AGenUI.getInstance();
                    for (int i = 0; i < DAYNIGHT_PER_ROUND; i++) {
                        sdk.setDayNightMode(i % 2 == 0 ? "light" : "dark");
                        toggles.incrementAndGet();
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[RISK24] Toggler exception round=" + r, e);
                }
            }, "risk24-toggler-" + round);

            streamer.start();
            toggler.start();
            streamer.join(30000);
            toggler.join(30000);

            Log.i(TAG, "[RISK24] Round " + r + " done: surfaces=" + surfaces.get()
                    + " toggles=" + toggles.get());

            // Cleanup
            runOnActivity(activity -> sm.destroy());
            Thread.sleep(100);
        }
        Log.i(TAG, "=== RISK24 completed (no native crash = race NOT hit) ===");
    }

    // ----------------------------------------------------------------
    // RISK25: concurrent updateComponents (via receiveTextChunk on test thread)
    //         + invalidateFunctionCallValues (via SM.invalidateFunctionCallValues
    //           → posted to worker thread).
    //
    // Creates a surface first, then concurrently:
    //   - Streams updateComponents on a background thread (modifies Surface
    //     component tree, BatchGuard state, Yoga layout tree)
    //   - Calls sm.invalidateFunctionCallValues() on another thread (posts to
    //     worker, where SurfaceCoordinator iterates _surfaces and reads Surface
    //     component state — same objects being modified by the updater thread)
    //
    // This is a realistic scenario: an SSE stream delivers component updates
    // while the app changes theme / locale, causing a full re-evaluation.
    //
    // Expected: SIGSEGV from corrupted component tree or BatchGuard state.
    // ----------------------------------------------------------------
    @Test
    public void testRISK25_concurrentUpdateAndInvalidate() throws Exception {
        final int ROUNDS = 15;
        final int UPDATES_PER_ROUND = 200;
        final int INVALIDATES_PER_ROUND = 500;

        Log.i(TAG, "=== RISK25: concurrent updateComponents + invalidate ===");

        for (int round = 0; round < ROUNDS; round++) {
            final int r = round;
            final SurfaceManager[] smHolder = new SurfaceManager[1];
            runOnActivity(activity -> smHolder[0] = new SurfaceManager(activity));
            final SurfaceManager sm = smHolder[0];

            // Create initial surface with a root component
            String surfaceId = "r25-" + r;
            String createJson = "{\"version\":\"v0.9\",\"createSurface\":"
                    + "{\"surfaceId\":\"" + surfaceId + "\",\"catalogId\":\"test\"}}";
            sm.beginTextStream();
            sm.receiveTextChunk(createJson);
            sm.endTextStream();

            // Send initial components to populate the surface
            String initComponents = "{\"version\":\"v0.9\",\"updateComponents\":"
                    + "{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                    + "{\"id\":\"root\",\"component\":\"Column\",\"children\":[\"t0\"],"
                    + "\"styles\":{\"width\":\"100%\",\"height\":\"auto\"}},"
                    + "{\"id\":\"t0\",\"component\":\"Text\",\"text\":\"initial\"}"
                    + "]}}";
            sm.beginTextStream();
            sm.receiveTextChunk(initComponents);
            sm.endTextStream();

            Thread.sleep(200); // Let the surface and initial components settle

            CyclicBarrier barrier = new CyclicBarrier(2);
            AtomicInteger updates = new AtomicInteger(0);
            AtomicInteger invalidations = new AtomicInteger(0);

            // Thread 1: rapid updateComponents
            Thread updater = new Thread(() -> {
                try {
                    barrier.await();
                    for (int i = 0; i < UPDATES_PER_ROUND; i++) {
                        // Add new components to the surface
                        String updateJson = "{\"version\":\"v0.9\",\"updateComponents\":"
                                + "{\"surfaceId\":\"" + surfaceId + "\",\"components\":["
                                + "{\"id\":\"c" + i + "\",\"component\":\"Text\","
                                + "\"text\":\"update-" + i + "\","
                                + "\"styles\":{\"fontSize\":\"" + (12 + i % 10) + "px\"}}"
                                + "]}}";
                        sm.beginTextStream();
                        sm.receiveTextChunk(updateJson);
                        sm.endTextStream();
                        updates.incrementAndGet();
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[RISK25] Updater exception round=" + r, e);
                }
            }, "risk25-updater-" + r);

            // Thread 2: rapid invalidateFunctionCallValues (posts to worker)
            Thread invalidator = new Thread(() -> {
                try {
                    barrier.await();
                    for (int i = 0; i < INVALIDATES_PER_ROUND; i++) {
                        sm.invalidateFunctionCallValues();
                        invalidations.incrementAndGet();
                        // Also mix in setDayNightMode for extra churn
                        if (i % 5 == 0) {
                            AGenUI.getInstance().setDayNightMode(
                                    i % 2 == 0 ? "light" : "dark");
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[RISK25] Invalidator exception round=" + r, e);
                }
            }, "risk25-invalidator-" + r);

            updater.start();
            invalidator.start();
            updater.join(30000);
            invalidator.join(30000);

            Log.i(TAG, "[RISK25] Round " + r + " done: updates=" + updates.get()
                    + " invalidations=" + invalidations.get());

            // Cleanup
            runOnActivity(activity -> sm.destroy());
            Thread.sleep(100);
        }
        Log.i(TAG, "=== RISK25 completed (no native crash = race NOT hit) ===");
    }

    // ----------------------------------------------------------------
    // RISK26: concurrent receiveTextChunk from TWO threads on the SAME SM.
    //
    // receiveTextChunk runs processDataAssembling directly on the caller
    // thread. StreamingContentParser and ProtocolStreamExtractor have NO
    // internal locks. Two threads calling processDataAssembling concurrently
    // corrupts the internal buffer and parser state.
    //
    // This is a user error (contract says "must be called on the same thread")
    // but the SDK should not SIGSEGV — it should either protect or fail
    // gracefully.
    //
    // Also tests: concurrent createSurface + deleteSurface via streaming
    // on two threads — both modify SurfaceCoordinator::_surfaces (std::map)
    // → red-black tree corruption → SIGSEGV.
    //
    // Expected: SIGSEGV from corrupted parser buffer or corrupted _surfaces map.
    // ----------------------------------------------------------------
    @Test
    public void testRISK26_concurrentReceiveTextChunkTwoThreads() throws Exception {
        final int ROUNDS = 20;
        final int OPS_PER_THREAD = 100;

        Log.i(TAG, "=== RISK26: concurrent receiveTextChunk from 2 threads ===");

        for (int round = 0; round < ROUNDS; round++) {
            final int r = round;
            final SurfaceManager[] smHolder = new SurfaceManager[1];
            runOnActivity(activity -> smHolder[0] = new SurfaceManager(activity));
            final SurfaceManager sm = smHolder[0];
            Thread.sleep(100); // Wait for SM init

            CyclicBarrier barrier = new CyclicBarrier(2);
            AtomicInteger opsA = new AtomicInteger(0);
            AtomicInteger opsB = new AtomicInteger(0);

            // Thread A: create surfaces rapidly
            Thread threadA = new Thread(() -> {
                try {
                    barrier.await();
                    for (int i = 0; i < OPS_PER_THREAD; i++) {
                        String json = "{\"version\":\"v0.9\",\"createSurface\":"
                                + "{\"surfaceId\":\"r26a-" + r + "-" + i + "\","
                                + "\"catalogId\":\"test\"}}";
                        sm.beginTextStream();
                        sm.receiveTextChunk(json);
                        sm.endTextStream();
                        opsA.incrementAndGet();
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[RISK26] ThreadA exception round=" + r, e);
                }
            }, "risk26-threadA-" + round);

            // Thread B: also create surfaces + delete surfaces on same SM
            Thread threadB = new Thread(() -> {
                try {
                    barrier.await();
                    for (int i = 0; i < OPS_PER_THREAD; i++) {
                        // Create a surface
                        String createJson = "{\"version\":\"v0.9\",\"createSurface\":"
                                + "{\"surfaceId\":\"r26b-" + r + "-" + i + "\","
                                + "\"catalogId\":\"test\"}}";
                        sm.beginTextStream();
                        sm.receiveTextChunk(createJson);
                        sm.endTextStream();

                        // Delete a surface from thread A (if it exists)
                        if (i > 5) {
                            String deleteJson = "{\"version\":\"v0.9\",\"deleteSurface\":"
                                    + "{\"surfaceId\":\"r26a-" + r + "-" + (i - 5) + "\"}}";
                            sm.beginTextStream();
                            sm.receiveTextChunk(deleteJson);
                            sm.endTextStream();
                        }
                        opsB.incrementAndGet();
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[RISK26] ThreadB exception round=" + r, e);
                }
            }, "risk26-threadB-" + round);

            threadA.start();
            threadB.start();
            threadA.join(30000);
            threadB.join(30000);

            Log.i(TAG, "[RISK26] Round " + r + " done: opsA=" + opsA.get()
                    + " opsB=" + opsB.get());

            // Cleanup
            runOnActivity(activity -> sm.destroy());
            Thread.sleep(50);
        }
        Log.i(TAG, "=== RISK26 completed (no native crash = race NOT hit) ===");
    }
}
