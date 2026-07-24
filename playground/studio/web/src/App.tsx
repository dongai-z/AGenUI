/** AGenUI Studio root: three-column layout + generation orchestration. */

import { useCallback, useEffect, useState } from "react";
import { ConversationPanel } from "@/components/Conversation/ConversationPanel";
import { Header } from "@/components/Header";
import { InputBar } from "@/components/InputBar/InputBar";
import { ProtocolPanel } from "@/components/Protocol/ProtocolPanel";
import { PresetSidebar, type Selection } from "@/components/Sidebar/PresetSidebar";
import { useGeneration } from "@/hooks/useGeneration";
import { useLibrary } from "@/hooks/useLibrary";
import { useProviders } from "@/hooks/useProviders";
import {
  fetchPreset,
  fetchProtocol,
  updateProtocol,
} from "@/api/client";
import type { A2uiPayload, RoundSnapshot } from "@/types";
import type { ChatMessage } from "@/api/sse";

interface PanelState {
  title: string;
  componentsText: string;
  datamodelText: string;
  protocolId: string | null;
  presetId: string | null;
  /** Whether the selected preset ships a reference rendering.png. */
  hasRendering: boolean;
}

const EMPTY_PANEL: PanelState = {
  title: "",
  componentsText: "",
  datamodelText: "",
  protocolId: null,
  presetId: null,
  hasRendering: false,
};

export default function App() {
  const gen = useGeneration();
  const { providers, active, serverInfo } = useProviders();
  const library = useLibrary();

  const [sidebarOpen, setSidebarOpen] = useState(true);
  const [selection, setSelection] = useState<Selection>(null);
  const [panel, setPanel] = useState<PanelState>(EMPTY_PANEL);
  const [history, setHistory] = useState<RoundSnapshot[]>([]);

  // --- Sync editor panel while streaming ---
  useEffect(() => {
    if (gen.isGenerating) {
      setSelection(null);
      setPanel((p) => ({
        ...p,
        title: gen.prompt || "Generating...",
        componentsText: gen.componentsText,
        datamodelText: gen.datamodelText,
        protocolId: null,
        presetId: null,
        hasRendering: false,
      }));
    }
  }, [gen.isGenerating, gen.componentsText, gen.datamodelText, gen.prompt]);

  // --- Finalize panel when generation completes ---
  useEffect(() => {
    if (gen.status === "done" && gen.done) {
      const d = gen.done;
      setPanel((p) => ({
        title: gen.prompt || p.title,
        componentsText: d.components ? JSON.stringify(d.components, null, 2) : p.componentsText,
        datamodelText: d.datamodel ? JSON.stringify(d.datamodel, null, 2) : p.datamodelText,
        protocolId: d.protocol_id,
        presetId: null,
        hasRendering: false,
      }));
      setSelection({ kind: "protocol", id: d.protocol_id });
      void library.refresh();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [gen.status]);

  // --- Archive the just-finished round into history when a new one starts ---
  const handleGenerate = useCallback(
    (prompt: string, provider: string | null, reasoning: boolean) => {
      // Build multi-turn history from the last completed round (single-turn context).
      const chatHistory: ChatMessage[] = [];
      if (gen.prompt && gen.done?.components) {
        chatHistory.push(
          { role: "user", content: gen.prompt },
          {
            role: "assistant",
            content: JSON.stringify(
              { updateComponents: gen.done.components, updateDataModel: gen.done.datamodel },
              null,
              2,
            ),
          },
        );
      }

      // Archive current round into display history.
      if (gen.prompt) {
        setHistory((h) => [
          ...h,
          {
            id: `${Date.now()}`,
            prompt: gen.prompt,
            model: gen.model,
            reasoning: gen.reasoning,
            thinking: gen.thinking,
            done: gen.done,
            error: gen.error,
          },
        ]);
      }
      gen.generate(prompt, "component", provider, reasoning, chatHistory);
    },
    [gen],
  );

  // --- New chat: clear conversation history ---
  const handleNewChat = useCallback(() => {
    setHistory([]);
  }, []);

  // --- Load a preset sample into the panel ---
  const handleSelectPreset = useCallback(async (id: string) => {
    try {
      const rec = await fetchPreset(id);
      setSelection({ kind: "preset", id });
      setPanel({
        title: rec.name,
        componentsText: rec.components ? JSON.stringify(rec.components, null, 2) : "",
        datamodelText: rec.datamodel ? JSON.stringify(rec.datamodel, null, 2) : "",
        protocolId: null,
        presetId: rec.id,
        hasRendering: rec.has_rendering ?? false,
      });
    } catch {
      // ignore load errors for now
    }
  }, []);

  // --- Load a generated protocol into the panel ---
  const handleSelectProtocol = useCallback(async (id: string) => {
    try {
      const rec = await fetchProtocol(id);
      setSelection({ kind: "protocol", id });
      setPanel({
        title: rec.prompt || rec.id,
        componentsText: rec.components ? JSON.stringify(rec.components, null, 2) : "",
        datamodelText: rec.datamodel ? JSON.stringify(rec.datamodel, null, 2) : "",
        protocolId: rec.id,
        presetId: null,
        hasRendering: false,
      });
    } catch {
      // ignore load errors for now
    }
  }, []);

  // --- Save manual edits (PUT) ---
  const handleSave = useCallback(
    async (components: A2uiPayload, datamodel: A2uiPayload | null) => {
      if (!panel.protocolId) throw new Error("No protocol to save");
      await updateProtocol(panel.protocolId, components, datamodel);
      void library.refresh();
    },
    [panel.protocolId, library],
  );

  // --- QR code URL ---
  const qrUrl =
    serverInfo && panel.protocolId
      ? `${serverInfo.base_url}/api/protocols/${panel.protocolId}/raw`
      : serverInfo && panel.presetId
        ? `${serverInfo.base_url}/api/presets/${panel.presetId}/raw`
        : null;

  // --- Reference rendering URL (presets only, when rendering.png exists) ---
  const renderingUrl =
    panel.presetId && panel.hasRendering
      ? `/api/presets/${encodeURIComponent(panel.presetId)}/rendering`
      : null;

  return (
    <div className="flex h-full flex-col overflow-hidden">
      <Header
        sidebarOpen={sidebarOpen}
        onToggleSidebar={() => setSidebarOpen((o) => !o)}
        serverInfo={serverInfo}
      />

      <div className="flex min-h-0 flex-1">
        {sidebarOpen && (
          <PresetSidebar
            presets={library.presets}
            protocols={library.protocols}
            loading={library.loading}
            selection={selection}
            onSelectPreset={handleSelectPreset}
            onSelectProtocol={handleSelectProtocol}
            onRefresh={library.refresh}
            onNewChat={handleNewChat}
          />
        )}

        {/* Conversation column */}
        <div className="flex min-w-[320px] flex-1 flex-col">
          <ConversationPanel
            history={history}
            live={{
              status: gen.status,
              prompt: gen.prompt,
              model: gen.model,
              currentStage: gen.currentStage,
              reasoning: gen.reasoning,
              thinking: gen.thinking,
              startedAt: gen.startedAt,
              done: gen.done,
              error: gen.error,
            }}
          />
          <InputBar
            providers={providers}
            active={active}
            isGenerating={gen.isGenerating}
            onSend={handleGenerate}
            onStop={gen.stop}
          />
        </div>

        {/* Protocol column (shares remaining space with the conversation) */}
        <div className="flex min-w-[360px] flex-1 flex-col">
          <ProtocolPanel
            title={panel.title}
            componentsText={panel.componentsText}
            datamodelText={panel.datamodelText}
            onComponentsChange={(v) => setPanel((p) => ({ ...p, componentsText: v }))}
            onDatamodelChange={(v) => setPanel((p) => ({ ...p, datamodelText: v }))}
            streaming={gen.isGenerating}
            protocolId={panel.protocolId}
            presetId={panel.presetId}
            renderingUrl={renderingUrl}
            qrUrl={qrUrl}
            onSave={handleSave}
          />
        </div>
      </div>
    </div>
  );
}
