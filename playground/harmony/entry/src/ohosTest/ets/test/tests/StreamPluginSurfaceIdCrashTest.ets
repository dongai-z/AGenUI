/**
 * StreamPluginSurfaceIdCrashTest.ets
 *
 * RISK60: TextStreamPlugin continueDataModelStreaming() surfaceId/path type-mismatch crash
 *
 * Vulnerability (agenui_text_stream_plugin.cpp line 306-308):
 *   std::string sid = udm["surfaceId"].get<std::string>();
 *   std::string ePath = udm.contains("path") ? udm["path"].get<std::string>() : "/";
 *
 * When streaming data model JSON completes, the plugin re-parses the full
 * JSON and calls .get<std::string>() on "surfaceId"/"path" WITHOUT type check.
 * Non-string surfaceId/path → nlohmann::type_error → std::terminate → crash.
 *
 * DIFFERENTIATION from RISK58/59:
 * - Location: stream/ layer (TextStreamPlugin), NOT surface/component_manager
 * - Trigger: Multi-chunk streaming with pre-registered Text data binding
 * - Independent fix: patching component_manager/surface does NOT fix this
 *
 * ATTACK FLOW:
 * 1. Send updateComponents with Text having data binding path {"text":{"path":"/msg"}}
 * 2. Send INCOMPLETE updateDataModel with non-string surfaceId (enters streaming mode)
 * 3. Send completion chunk → continueDataModelStreaming() re-parses full JSON → crash
 *
 * Shared core/ code — affects Android, iOS, and HarmonyOS.
 *
 * Test cases:
 *   RISK60-01  surfaceId: 12345 (integer) → crash
 *   RISK60-02  surfaceId: null → crash
 *   RISK60-03  path: [1,2,3] (array) → crash
 */

import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { common } from '@kit.AbilityKit';
import { SurfaceManager } from '@agenui/agenui';
import {
  sendAndWaitForSurface,
  sleep,
  TIMEOUT_MS
} from '../base/AGenUIBaseTest';

export default function streamPluginSurfaceIdCrashTest() {
  describe('StreamPluginSurfaceIdCrashTest', () => {
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
     * RISK60-01: surfaceId as integer in streaming DM completion.
     *
     * Multi-chunk attack:
     * - Chunk 1: createSurface + updateComponents with Text data binding
     * - Chunk 2: INCOMPLETE updateDataModel with integer surfaceId
     * - Chunk 3: Completion of JSON → continueDataModelStreaming() → crash
     */
    it('RISK60-01_streaming_surfaceId_integer_crash', 0, async () => {
      const surfaceId = 's_r60_hm_t1';

      // Create surface
      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Step 1: Register Text component with data binding path "/msg"
      // This registers the path in _textBindingPaths within TextStreamPlugin
      sm.beginTextStream();
      sm.receiveTextChunk(`{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"root","component":"Column","children":["t1"]},{"id":"t1","component":"Text","text":{"path":"/msg"}}]}}`);
      sm.endTextStream();
      await sleep(500);

      // Step 2: Begin new stream and send INCOMPLETE updateDataModel with integer surfaceId
      // The incomplete JSON enters streaming mode via handleIncompleteDataModel()
      // which uses text search (returns "" for non-string surfaceId) - does NOT crash here
      sm.beginTextStream();
      sm.receiveTextChunk(`{"version":"v0.9","updateDataModel":{"surfaceId":12345,"path":"/msg","value":"Hello worl`);
      await sleep(300);

      // Step 3: Complete the JSON → continueDataModelStreaming() re-parses with nlohmann JSON
      // udm["surfaceId"].get<std::string>() throws type_error → crash
      sm.receiveTextChunk(`d"}}`);
      sm.endTextStream();

      await sleep(2000);
      // If process survives, bug is not present or was fixed
      expect(true).assertTrue();
    });

    /**
     * RISK60-02: surfaceId as null in streaming DM completion.
     */
    it('RISK60-02_streaming_surfaceId_null_crash', 0, async () => {
      const surfaceId = 's_r60_hm_t2';

      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Register Text with data binding
      sm.beginTextStream();
      sm.receiveTextChunk(`{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"root","component":"Column","children":["t1"]},{"id":"t1","component":"Text","text":{"path":"/msg"}}]}}`);
      sm.endTextStream();
      await sleep(500);

      // Incomplete updateDataModel with null surfaceId
      sm.beginTextStream();
      sm.receiveTextChunk(`{"version":"v0.9","updateDataModel":{"surfaceId":null,"path":"/msg","value":"Stream tex`);
      await sleep(300);

      // Complete → crash in continueDataModelStreaming()
      sm.receiveTextChunk(`t"}}`);
      sm.endTextStream();

      await sleep(2000);
      expect(true).assertTrue();
    });

    /**
     * RISK60-03: path as array in streaming DM completion.
     *
     * Even if surfaceId is a valid string, non-string "path" field also triggers
     * the same crash at: udm["path"].get<std::string>()
     */
    it('RISK60-03_streaming_path_array_crash', 0, async () => {
      const surfaceId = 's_r60_hm_t3';

      const createJson = `{"version":"v0.9","createSurface":{"surfaceId":"${surfaceId}","catalogId":"test"}}`;
      await sendAndWaitForSurface(sm, createJson, surfaceId);

      // Register Text with data binding at "/"
      sm.beginTextStream();
      sm.receiveTextChunk(`{"version":"v0.9","updateComponents":{"surfaceId":"${surfaceId}","components":[{"id":"root","component":"Column","children":["t1"]},{"id":"t1","component":"Text","text":{"path":"/"}}]}}`);
      sm.endTextStream();
      await sleep(500);

      // Incomplete updateDataModel with array path
      sm.beginTextStream();
      sm.receiveTextChunk(`{"version":"v0.9","updateDataModel":{"surfaceId":"${surfaceId}","path":[1,2,3],"value":"Crash tex`);
      await sleep(300);

      // Complete → crash at path.get<string>()
      sm.receiveTextChunk(`t"}}`);
      sm.endTextStream();

      await sleep(2000);
      expect(true).assertTrue();
    });
  });
}
