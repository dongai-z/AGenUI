import XCTest
@testable import AGenUI

/**
 * RISK59: parseComponent "rawId" type-mismatch → nlohmann::type_error → SIGABRT
 *
 * Vulnerability (agenui_component_manager.cpp line 347-348):
 *   if (json.contains("rawId")) {
 *       rawId = json["rawId"].get<std::string>();  // ← THROWS if rawId is non-string
 *   }
 *
 * Distinct from RISK40 ("id":null crash) because:
 * - RISK40 targets json["id"].get<std::string>() with null/non-string id
 * - RISK59 targets json["rawId"].get<std::string>() with valid string id but non-string rawId
 * - A fix for RISK40 does NOT fix this vulnerability
 *
 * Shared core/ code — same crash on iOS as Android/Harmony.
 */
final class SDKRiskProbeRawIdTypeMismatchTest: XCTestCase {

    private var sm: SurfaceManager!

    override func setUp() {
        super.setUp()
        sm = SurfaceManager()
    }

    override func tearDown() {
        sm = nil
        super.tearDown()
    }

    /// RISK59 Test 1: "rawId": 12345 (integer) with valid string "id" and "component"
    func testRISK59_rawIdInteger() {
        let surfaceId = "s_r59_ios_t1"

        // Create surface
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        let createWait = expectation(description: "create")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { createWait.fulfill() }
        wait(for: [createWait], timeout: 2.0)

        // Attack: valid id + valid component + rawId as integer
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"valid_c1","component":"Text","rawId":12345}]}}
        """)
        sm.endTextStream()

        // Wait — if process survives, bug is fixed
        let attackWait = expectation(description: "attack")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { attackWait.fulfill() }
        wait(for: [attackWait], timeout: 5.0)
    }

    /// RISK59 Test 2: "rawId": null with valid string "id" and "component"
    func testRISK59_rawIdNull() {
        let surfaceId = "s_r59_ios_t2"

        // Create surface
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        let createWait = expectation(description: "create")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { createWait.fulfill() }
        wait(for: [createWait], timeout: 2.0)

        // Attack: valid id + valid component + rawId as null
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"valid_c2","component":"Text","rawId":null}]}}
        """)
        sm.endTextStream()

        let attackWait = expectation(description: "attack")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { attackWait.fulfill() }
        wait(for: [attackWait], timeout: 5.0)
    }

    /// RISK59 Test 3: "rawId": [1,2,3] (array) with valid string "id" and "component"
    func testRISK59_rawIdArray() {
        let surfaceId = "s_r59_ios_t3"

        // Create surface
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"createSurface":{"surfaceId":"\(surfaceId)","catalogId":"test"}}
        """)
        sm.endTextStream()

        let createWait = expectation(description: "create")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { createWait.fulfill() }
        wait(for: [createWait], timeout: 2.0)

        // Attack: valid id + valid component + rawId as array
        sm.beginTextStream()
        sm.receiveTextChunk("""
        {"version":"v0.9","updateComponents":{"surfaceId":"\(surfaceId)","components":[{"id":"valid_c3","component":"Column","rawId":[1,2,3]}]}}
        """)
        sm.endTextStream()

        let attackWait = expectation(description: "attack")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { attackWait.fulfill() }
        wait(for: [attackWait], timeout: 5.0)
    }
}
