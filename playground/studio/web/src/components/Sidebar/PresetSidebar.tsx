/** Left collapsible sidebar: preset samples + the user's generated protocols. */

import { useMemo, useState } from "react";
import {
  BoxIcon,
  ChevronDownIcon,
  ClockIcon,
  RefreshIcon,
  SearchIcon,
} from "@/components/icons";
import { cn, formatTime } from "@/lib/utils";
import type { PresetSummary, ProtocolSummary } from "@/types";

export type Selection =
  | { kind: "preset"; id: string }
  | { kind: "protocol"; id: string }
  | null;

interface PresetSidebarProps {
  presets: PresetSummary[];
  protocols: ProtocolSummary[];
  loading: boolean;
  selection: Selection;
  onSelectPreset: (id: string) => void;
  onSelectProtocol: (id: string) => void;
  onRefresh: () => void;
}

function GroupHeader({
  icon,
  label,
  count,
  open,
  onToggle,
}: {
  icon: React.ReactNode;
  label: string;
  count: number;
  open: boolean;
  onToggle: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onToggle}
      className="flex w-full items-center gap-1.5 px-2 py-1.5 text-[11px] font-semibold uppercase tracking-wide text-slate-400 hover:text-slate-600"
    >
      <ChevronDownIcon
        size={12}
        className={cn("transition-transform", !open && "-rotate-90")}
      />
      {icon}
      {label}
      <span className="ml-auto rounded-full bg-slate-100 px-1.5 text-[10px] font-medium text-slate-400">
        {count}
      </span>
    </button>
  );
}

export function PresetSidebar({
  presets,
  protocols,
  loading,
  selection,
  onSelectPreset,
  onSelectProtocol,
  onRefresh,
}: PresetSidebarProps) {
  const [query, setQuery] = useState("");
  const [presetsOpen, setPresetsOpen] = useState(true);
  const [protocolsOpen, setProtocolsOpen] = useState(true);

  const q = query.trim().toLowerCase();
  const filteredPresets = useMemo(
    () => (q ? presets.filter((p) => p.name.toLowerCase().includes(q)) : presets),
    [presets, q],
  );
  const filteredProtocols = useMemo(
    () =>
      q
        ? protocols.filter((p) => p.prompt_summary.toLowerCase().includes(q))
        : protocols,
    [protocols, q],
  );

  return (
    <div className="flex h-full w-60 shrink-0 flex-col border-r border-slate-200 bg-white">
      {/* Search + refresh */}
      <div className="flex shrink-0 items-center gap-1.5 border-b border-slate-200 p-2">
        <div className="flex flex-1 items-center gap-1.5 rounded-lg border border-slate-200 bg-slate-50 px-2 py-1.5 focus-within:border-brand-500 focus-within:bg-white">
          <SearchIcon size={13} className="text-slate-400" />
          <input
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Search protocols"
            className="w-full bg-transparent text-xs text-slate-700 outline-none placeholder:text-slate-400"
          />
        </div>
        <button
          type="button"
          onClick={onRefresh}
          title="Refresh list"
          className="rounded-lg border border-slate-200 p-1.5 text-slate-400 transition hover:border-slate-300 hover:text-slate-600"
        >
          <RefreshIcon size={13} className={cn(loading && "animate-spin")} />
        </button>
      </div>

      <div className="min-h-0 flex-1 overflow-y-auto p-1.5">
        {/* Preset samples */}
        <GroupHeader
          icon={<BoxIcon size={12} />}
          label="Presets"
          count={filteredPresets.length}
          open={presetsOpen}
          onToggle={() => setPresetsOpen((o) => !o)}
        />
        {presetsOpen && (
          <div className="mb-2 space-y-0.5">
            {filteredPresets.length === 0 && (
              <p className="px-2 py-1 text-[11px] text-slate-400">No presets found</p>
            )}
            {filteredPresets.map((p) => {
              const active = selection?.kind === "preset" && selection.id === p.id;
              return (
                <button
                  key={p.id}
                  type="button"
                  onClick={() => onSelectPreset(p.id)}
                  className={cn(
                    "flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left text-xs transition",
                    active
                      ? "bg-brand-50 font-medium text-brand-600"
                      : "text-slate-600 hover:bg-slate-100",
                  )}
                >
                  <BoxIcon size={13} className={active ? "text-brand-500" : "text-slate-300"} />
                  <span className="truncate">{p.name}</span>
                </button>
              );
            })}
          </div>
        )}

        {/* My generations */}
        <GroupHeader
          icon={<ClockIcon size={12} />}
          label="My Generations"
          count={filteredProtocols.length}
          open={protocolsOpen}
          onToggle={() => setProtocolsOpen((o) => !o)}
        />
        {protocolsOpen && (
          <div className="space-y-0.5">
            {filteredProtocols.length === 0 && (
              <p className="px-2 py-1 text-[11px] text-slate-400">
                Generated protocols will appear here
              </p>
            )}
            {filteredProtocols.map((p) => {
              const active = selection?.kind === "protocol" && selection.id === p.id;
              return (
                <button
                  key={p.id}
                  type="button"
                  onClick={() => onSelectProtocol(p.id)}
                  title={p.prompt_summary}
                  className={cn(
                    "flex w-full flex-col gap-0.5 rounded-md px-2 py-1.5 text-left transition",
                    active ? "bg-brand-50" : "hover:bg-slate-100",
                  )}
                >
                  <span
                    className={cn(
                      "truncate text-xs",
                      active ? "font-medium text-brand-600" : "text-slate-600",
                    )}
                  >
                    {p.prompt_summary || "(no prompt)"}
                  </span>
                  <span className="text-[10px] text-slate-400">
                    {p.provider}/{p.model} · {formatTime(p.created_at)}
                  </span>
                </button>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}
