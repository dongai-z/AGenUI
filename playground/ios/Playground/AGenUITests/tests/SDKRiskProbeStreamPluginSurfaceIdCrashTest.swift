import XCTest
@testable import AGenUI

/**
 * RISK60: TextStreamPlugin continueDataModelStreaming() surfaceId type-mismatch crash
 *
 * Vulnerability (agenui_text_stream_plugin.cpp line 306):
 *   std::string sid = udm["surfaceId"].get<std::string>();
 *
 * When streaming data model JSON completes, the plugin re-parses the full
 * JSON and calls .get<std::string>() on "surfaceId" WITHOUT type check.
 * Non-string surfaceId → nlohmann::type_error → std::terminate → crash.
 *
 * DIFFERENTIATION from RISK40-59:
 * - Location: stream/ layer (TextStreamPlugin), NOT surface/component_manager
 * - Trigger: Multi-chunk streaming with pre-registered Text data binding
 * - Independent fix: patching component_manager/surface does NOT fix this
 *
 * ATTACK FLOW:
 * 1. Send updateComponents with Text having data binding path
 * 2. Send INCOMPLETE updateDataModel with non-string surfaceId
 * 3. Send completion → crash in continueDataModelStreaming()
 *
 * Shared core/ code — affects Android, iOS, and HarmonyOS.
 */
final class SDKRiskProbeStreamPluginSurfaceIdCrashTest: XCTestCase {

    private var sm: SurfaceManager!

    override func setUp() {
        super.setUp()
        sm = SurfaceManager()
    }

    override func tearDown() {
        sm = nil
        super.tearDown()
    }

    /// RISK60 Test 1: surfaceId as integer in streaming DM completion
    func testRISK60_streamingSurfaceIdInteger() {
        let surfaceId = "s_r60_ios_t1"

        // Create surface
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        let createWait = expectation(description: "create")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { createWait.fulfill() }
        wait(for: [createWait], timeout: 2.0)

        // Step 1: Register Text component with data binding path "/msg"
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"root","component":"Column","children":["t1"]},{"id":"t1","component":"Text","text":{"path":"/msg"}}]}}
        """)

        let setupWait = expectation(description: "setup")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { setupWait.fulfill() }
        wait(for: [setupWait], timeout: 2.0)

        // Step 2: Send INCOMPLETE updateDataModel with integer surfaceId
        sm.receiveTextChunk("""
        {"version":"v0.9","updateDataModel":{"surfaceId":12345,"path":"/msg","value":"Hello worl
        """)

        let streamWait = expectation(description: "stream")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { streamWait.fulfill() }
        wait(for: [streamWait], timeout: 2.0)

        // Step 3: Complete the JSON → crash in continueDataModelStreaming()
        sm.receiveTextChunk("d\"}}")
        sm.endTextStream()

        let crashWait = expectation(description: "crash")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { crashWait.fulfill() }
        wait(for: [crashWait], timeout: 5.0)
    }

    /// RISK60 Test 2: surfaceId as null in streaming DM completion
    func testRISK60_streamingSurfaceIdNull() {
        let surfaceId = "s_r60_ios_t2"

        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        let createWait = expectation(description: "create")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { createWait.fulfill() }
        wait(for: [createWait], timeout: 2.0)

        // Step 1: Register Text with data binding
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"root","component":"Column","children":["t1"]},{"id":"t1","component":"Text","text":{"path":"/msg"}}]}}
        """)

        let setupWait = expectation(description: "setup")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { setupWait.fulfill() }
        wait(for: [setupWait], timeout: 2.0)

        // Step 2: Incomplete updateDataModel with null surfaceId
        sm.receiveTextChunk("""
        {"version":"v0.9","updateDataModel":{"surfaceId":null,"path":"/msg","value":"Stream tex
        """)

        let streamWait = expectation(description: "stream")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { streamWait.fulfill() }
        wait(for: [streamWait], timeout: 2.0)

        // Step 3: Complete → crash
        sm.receiveTextChunk("t\"}}")
        sm.endTextStream()

        let crashWait = expectation(description: "crash")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { crashWait.fulfill() }
        wait(for: [crashWait], timeout: 5.0)
    }

    /// RISK60 Test 3: path as array in streaming DM completion
    func testRISK60_streamingPathArray() {
        let surfaceId = "s_r60_ios_t3"

        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        let createWait = expectation(description: "create")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { createWait.fulfill() }
        wait(for: [createWait], timeout: 2.0)

        // Step 1: Register Text with data binding at "/"
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"root","component":"Column","children":["t1"]},{"id":"t1","component":"Text","text":{"path":"/"}}]}}
        """)

        let setupWait = expectation(description: "setup")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { setupWait.fulfill() }
        wait(for: [setupWait], timeout: 2.0)

        // Step 2: Incomplete updateDataModel with array path
        sm.receiveTextChunk("""
        {"version":"v0.9","updateDataModel":{"surfaceId":"\(surfaceId)","path":[1,2,3],"value":"Crash tex
        """)

        let streamWait = expectation(description: "stream")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { streamWait.fulfill() }
        wait(for: [streamWait], timeout: 2.0)

        // Step 3: Complete → crash at path.get<string>()
        sm.receiveTextChunk("t\"}}")
        sm.endTextStream()

        let crashWait = expectation(description: "crash")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { crashWait.fulfill() }
        wait(for: [crashWait], timeout: 5.0)
    }
}
