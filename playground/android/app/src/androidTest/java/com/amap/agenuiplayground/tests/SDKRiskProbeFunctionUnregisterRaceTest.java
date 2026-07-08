package com.amap.agenuiplayground.tests;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.amap.agenui.AGenUI;
import com.amap.agenui.function.FunctionCallContext;
import com.amap.agenui.function.FunctionConfig;
import com.amap.agenui.function.FunctionResult;
import com.amap.agenui.function.IFunction;
import com.amap.agenui.render.component.A2UIComponent;
import com.amap.agenui.render.surface.Surface;
import com.amap.agenuiplayground.base.AGenUIBaseTest;
import com.amap.agenuiplayground.base.TestFixtureLoader;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public class SDKRiskProbeFunctionUnregisterRaceTest extends AGenUIBaseTest {

    private static final String FUNCTION_NAME = "toast";
    private static final long ROUND_DURATION_MS = 5000L;
    private static final long JOIN_TIMEOUT_MS = 3000L;

    @Test
    public void testSDKRISK03_functionExecuteVsUnregisterRace() throws Exception {
        TestFixtureLoader loader = new TestFixtureLoader(
                androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().getTargetContext());
        String fixturePath = "function_call/action_toast.json";
        String surfaceId = loader.getSurfaceId(fixturePath);
        String json = loader.loadMessagesAsString(fixturePath);

        Surface surface = sendAndWaitForRender(json, surfaceId);
        assertNotNull("Surface should render for race probe", surface);

        A2UIComponent button = surface.getComponent("toast-btn");
        assertNotNull("toast-btn should exist", button);

        AtomicBoolean stop = new AtomicBoolean(false);
        AtomicInteger actionCount = new AtomicInteger(0);
        AtomicInteger executeCount = new AtomicInteger(0);
        AtomicInteger registerCount = new AtomicInteger(0);
        AtomicBoolean hadWorkerFailure = new AtomicBoolean(false);

        Runnable registerLoop = () -> {
            try {
                while (!stop.get()) {
                    AGenUI.getInstance().registerFunction(new SlowToastFunction(executeCount));
                    registerCount.incrementAndGet();
                    AGenUI.getInstance().unregisterFunction(FUNCTION_NAME);
                }
            } catch (Throwable t) {
                hadWorkerFailure.set(true);
            }
        };

        Thread registrar = new Thread(registerLoop, "sdk-risk-register-loop");
        registrar.start();

        long deadline = System.currentTimeMillis() + ROUND_DURATION_MS;
        while (System.currentTimeMillis() < deadline) {
            runOnActivity(activity -> button.triggerAction());
            actionCount.incrementAndGet();
        }

        stop.set(true);
        registrar.join(JOIN_TIMEOUT_MS);
        AGenUI.getInstance().unregisterFunction(FUNCTION_NAME);

        assertTrue("Registrar worker should not fail at Java layer", !hadWorkerFailure.get());
        assertTrue("Expected to dispatch at least one action", actionCount.get() > 0);
        assertTrue("Expected at least one register/unregister iteration", registerCount.get() > 0);
    }

    private static final class SlowToastFunction implements IFunction {
        private final AtomicInteger executeCount;

        private SlowToastFunction(AtomicInteger executeCount) {
            this.executeCount = executeCount;
        }

        @Override
        public FunctionResult execute(FunctionCallContext context, String jsonString) {
            executeCount.incrementAndGet();
            try {
                Thread.sleep(2L);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            return FunctionResult.createSuccess("{\"result\":\"ok\"}");
        }

        @Override
        public FunctionConfig getConfig() {
            return new FunctionConfig(FUNCTION_NAME);
        }
    }
}
