/**
 * RawIdTypeMismatchTest.ets
 *
 * RISK59: parseComponent "rawId" type-mismatch → nlohmann::type_error → SIGABRT
 *
 * Vulnerability (agenui_component_manager.cpp line 347-348):
 * ComponentManager::parseComponent() checks json.contains("rawId") (key existence)
 * but NOT json["rawId"].is_string(). Non-string "rawId" with valid string "id" and
 * "component" triggers get<std::string>() → uncaught type_error → std::terminate.
 *
 * Distinct from RISK40 (which tested "id": null) — here "id" is a valid string,
 * but "rawId" carries the wrong type. A fix for RISK40's "id" field would NOT
 * protect the "rawId" path.
 *
 * Shared core/ code — same crash on HarmonyOS as Android/iOS.
 *
 * Test cases:
 *   RISK59-01  "rawId": 12345 (integer) → crash
 *   RISK59-02  "rawId": null → crash
 *   RISK59-03  "rawId": [1,2,3] (array) → crash
 */

import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { common } from '@kit.AbilityKit';
import { SurfaceManager } from '@agenui/agenui';
import {
  sendAndWaitForSurface,
  sleep,
  TIMEOUT_MS
} from '../base/AGenUIBaseTest';

export default function rawIdTypeMismatchTest() {
  describe('RawIdTypeMismatchTest', () => {
    let sm: SurfaceManager;
    let context: common.UIAbilityContext;

    beforeEach(() => {
      context = getContext() as common.UIAbilityContext;
      sm = new SurfaceManager(context);
    });

    afterEach(() => {
      sm.destroy();
    });

    /**
     * RISK59-01: "rawId": 12345 (integer) with valid "id" and "component".
     *
     * json.contains("rawId") → true
     * json["rawId"].get<std::string>() → throws nlohmann::type_error
     *   (type must be string, but is number)
     * → std::terminate() → process killed
     */
    it('RISK59-01_rawId_integer_crash', 0, async () => {
      const surfaceId = 's_r59_hm_t1';

      // Create surface
      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Attack: valid "id" + valid "component" + "rawId": 12345 (integer)
      // Passes surface-level validation (component is string), reaches parseComponent()
      // parseComponent sees json.contains("rawId")=true, calls get<std::string>() → crash
      const attackJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"valid_c1","component":"Text","rawId":12345}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(attackJson);
      sm.endTextStream();

      await sleep(2000);
      // If process survives, bug is not present or was fixed
      expect(true).assertTrue();
    });

    /**
     * RISK59-02: "rawId": null with valid "id" and "component".
     *
     * json.contains("rawId") → true (key exists, value is null)
     * json["rawId"].get<std::string>() → throws nlohmann::type_error
     *   (type must be string, but is null)
     * → std::terminate() → process killed
     */
    it('RISK59-02_rawId_null_crash', 0, async () => {
      const surfaceId = 's_r59_hm_t2';

      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Attack: valid "id" + valid "component" + "rawId": null
      const attackJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"valid_c2","component":"Text","rawId":null}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(attackJson);
      sm.endTextStream();

      await sleep(2000);
      expect(true).assertTrue();
    });

    /**
     * RISK59-03: "rawId": [1,2,3] (array) with valid "id" and "component".
     *
     * json.contains("rawId") → true
     * json["rawId"].get<std::string>() → throws nlohmann::type_error
     *   (type must be string, but is array)
     * → std::terminate() → process killed
     */
    it('RISK59-03_rawId_array_crash', 0, async () => {
      const surfaceId = 's_r59_hm_t3';

      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Attack: valid "id" + valid "component" + "rawId": [1,2,3] (array)
      const attackJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"valid_c3","component":"Column","rawId":[1,2,3]}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(attackJson);
      sm.endTextStream();

      await sleep(2000);
      expect(true).assertTrue();
    });
  });
}
