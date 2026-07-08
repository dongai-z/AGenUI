import XCTest
@testable import AGenUI

/**
 * RISK58: tryApplyTextChunk "id" type-mismatch → nlohmann::type_error → SIGABRT
 *
 * Vulnerability (introduced commit 9dca725e):
 * ComponentManager::tryApplyTextChunk() checks json.contains("id") (key existence)
 * but NOT json["id"].is_string(). Non-string "id" with "textChunk" present triggers
 * get<std::string>() → uncaught type_error → std::terminate → SIGABRT.
 *
 * Shared core/ code — same crash on iOS as Android/Harmony.
 */
final class SDKRiskProbeTextChunkTypeMismatchTest: XCTestCase {

    private var sm: SurfaceManager!

    override func setUp() {
        super.setUp()
        sm = SurfaceManager()
    }

    override func tearDown() {
        sm = nil
        super.tearDown()
    }

    /// RISK58 Test 1: "id": null with "textChunk" present → crash in tryApplyTextChunk
    func testRISK58_textChunkIdNull() {
        let surfaceId = "s_r58_ios_t1"

        // Create surface
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        // Setup: create a valid Text component
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"txt1","component":"Text","attributes":{"text":"\\"hello\\""}}]}}
        """)
        sm.endTextStream()

        // Wait for component creation
        let setupWait = expectation(description: "setup")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { setupWait.fulfill() }
        wait(for: [setupWait], timeout: 2.0)

        // Attack: "id": null + "textChunk" present + "component":"Text" — hits tryApplyTextChunk path
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":null,"component":"Text","textChunk":"world"}]}}
        """)
        sm.endTextStream()

        // If we reach here, the process survived (unexpected when bug exists)
        let survive = expectation(description: "survive")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { survive.fulfill() }
        wait(for: [survive], timeout: 5.0)

        XCTAssert(true, "RISK58: Process survived null-id textChunk (unexpected if vulnerable)")
    }

    /// RISK58 Test 2: "id": 12345 (integer) with "textChunk" present
    func testRISK58_textChunkIdInteger() {
        let surfaceId = "s_r58_ios_t2"

        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"txt1","component":"Text","attributes":{"text":"\\"hello\\""}}]}}
        """)
        sm.endTextStream()

        let setupWait = expectation(description: "setup")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { setupWait.fulfill() }
        wait(for: [setupWait], timeout: 2.0)

        // Attack: "id": 12345 (integer) + "component":"Text"
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":12345,"component":"Text","textChunk":"world"}]}}
        """)
        sm.endTextStream()

        let survive = expectation(description: "survive")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { survive.fulfill() }
        wait(for: [survive], timeout: 5.0)

        XCTAssert(true, "RISK58: Process survived integer-id textChunk (unexpected if vulnerable)")
    }

    /// RISK58 Test 3: "id": ["a","b"] (array) with "textChunk" present
    func testRISK58_textChunkIdArray() {
        let surfaceId = "s_r58_ios_t3"

        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"txt1","component":"Text","attributes":{"text":"\\"hello\\""}}]}}
        """)
        sm.endTextStream()

        let setupWait = expectation(description: "setup")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { setupWait.fulfill() }
        wait(for: [setupWait], timeout: 2.0)

        // Attack: "id": ["a","b"] (array) + "component":"Text"
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":["a","b"],"component":"Text","textChunk":"world"}]}}
        """)
        sm.endTextStream()

        let survive = expectation(description: "survive")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { survive.fulfill() }
        wait(for: [survive], timeout: 5.0)

        XCTAssert(true, "RISK58: Process survived array-id textChunk (unexpected if vulnerable)")
    }
}
