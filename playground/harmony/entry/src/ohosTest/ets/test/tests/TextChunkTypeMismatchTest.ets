/**
 * TextChunkTypeMismatchTest.ets
 *
 * RISK58: tryApplyTextChunk "id" type-mismatch → nlohmann::type_error → SIGABRT
 *
 * Vulnerability (introduced commit 9dca725e):
 * ComponentManager::tryApplyTextChunk() checks json.contains("id") (key existence)
 * but NOT json["id"].is_string(). Non-string "id" with "textChunk" present triggers
 * get<std::string>() → uncaught type_error → std::terminate → process killed.
 *
 * Shared core/ code — same crash on HarmonyOS as Android/iOS.
 *
 * Test cases:
 *   RISK58-01  "id": null + "textChunk" present → crash
 *   RISK58-02  "id": 12345 (integer) + "textChunk" present → crash
 *   RISK58-03  "id": ["a","b"] (array) + "textChunk" present → crash
 */

import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { common } from '@kit.AbilityKit';
import { SurfaceManager } from '@agenui/agenui';
import {
  sendAndWaitForSurface,
  sleep,
  TIMEOUT_MS
} from '../base/AGenUIBaseTest';

export default function textChunkTypeMismatchTest() {
  describe('TextChunkTypeMismatchTest', () => {
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
     * RISK58-01: "id": null with "textChunk" present.
     *
     * json.contains("id") → true (key exists, value is null)
     * json.contains("textChunk") → true
     * json["id"].get<std::string>() → throws nlohmann::type_error
     * → std::terminate() → process killed
     */
    it('RISK58-01_textChunk_id_null_crash', 0, async () => {
      const surfaceId = 's_r58_hm_t1';

      // Create surface
      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Setup: create a valid Text component
      const setupJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"txt1","component":"Text","attributes":{"text":"\\"hello\\""}}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(setupJson);
      sm.endTextStream();
      await sleep(500);

      // Attack: "id": null + "textChunk" present + "component":"Text"
      // Hits tryApplyTextChunk() path BEFORE parseComponent()
      const attackJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":null,"component":"Text","textChunk":"world"}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(attackJson);
      sm.endTextStream();

      await sleep(2000);
      // If process survives, bug is not present or was fixed
      expect(true).assertTrue();
    });

    /**
     * RISK58-02: "id": 12345 (integer) with "textChunk" present.
     */
    it('RISK58-02_textChunk_id_integer_crash', 0, async () => {
      const surfaceId = 's_r58_hm_t2';

      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      const setupJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"txt1","component":"Text","attributes":{"text":"\\"hello\\""}}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(setupJson);
      sm.endTextStream();
      await sleep(500);

      // Attack: "id": 12345 (integer, not string) + "component":"Text"
      const attackJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":12345,"component":"Text","textChunk":"world"}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(attackJson);
      sm.endTextStream();

      await sleep(2000);
      expect(true).assertTrue();
    });

    /**
     * RISK58-03: "id": ["a","b"] (array) with "textChunk" present.
     */
    it('RISK58-03_textChunk_id_array_crash', 0, async () => {
      const surfaceId = 's_r58_hm_t3';

      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      const setupJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"txt1","component":"Text","attributes":{"text":"\\"hello\\""}}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(setupJson);
      sm.endTextStream();
      await sleep(500);

      // Attack: "id": ["a","b"] (array, not string) + "component":"Text"
      const attackJson = `{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":["a","b"],"component":"Text","textChunk":"world"}]}}`;
      sm.beginTextStream();
      sm.receiveTextChunk(attackJson);
      sm.endTextStream();

      await sleep(2000);
      expect(true).assertTrue();
    });
  });
}
