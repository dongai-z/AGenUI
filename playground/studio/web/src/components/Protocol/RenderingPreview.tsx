/** Collapsible reference-rendering preview shown only for preset protocols.
 *
 * Presets may ship a `rendering.png` alongside their payloads — a reference of
 * what the protocol renders to. This section surfaces it above the protocol
 * editors so users can compare against the live Playground render. Non-preset
 * protocols (user generations) render nothing here.
 */

import { useEffect, useState } from "react";
import { ChevronDownIcon, ImageIcon, ImageOffIcon } from "@/components/icons";
import { cn } from "@/lib/utils";

interface RenderingPreviewProps {
  /** Present only when a preset is selected; null hides the whole section. */
  presetId: string | null;
  /** URL of rendering.png; null (for a preset) means no image available. */
  renderingUrl: string | null;
}

export function RenderingPreview({ presetId, renderingUrl }: RenderingPreviewProps) {
  const [open, setOpen] = useState(true);

  // Re-expand whenever a different preset is selected so its reference is
  // immediately visible (the whole point of picking a preset).
  useEffect(() => {
    setOpen(true);
  }, [presetId]);

  // Only preset protocols have a reference-rendering area.
  if (!presetId) return null;

  return (
    <div className="shrink-0 border-b border-slate-200 bg-white">
      {/* Section header — collapse toggle (mirrors the sidebar group style). */}
      <button
        type="button"
        onClick={() => setOpen((o) => !o)}
        className="flex w-full items-center gap-1.5 px-3 py-1.5 text-[11px] font-semibold uppercase tracking-wide text-slate-400 transition hover:text-slate-600"
      >
        <ChevronDownIcon
          size={12}
          className={cn("transition-transform", !open && "-rotate-90")}
        />
        <ImageIcon size={12} />
        Reference Rendering
      </button>

      {open && (
        <div className="px-3 pb-2.5">
          {renderingUrl ? (
            <a
              href={renderingUrl}
              target="_blank"
              rel="noopener noreferrer"
              title="Open full size"
              className="block cursor-zoom-in overflow-hidden rounded-lg border border-slate-200 bg-slate-100/70"
            >
              <img
                src={renderingUrl}
                alt="Reference rendering"
                className="mx-auto max-h-[220px] w-auto max-w-full object-contain"
              />
            </a>
          ) : (
            <div className="flex items-center justify-center gap-1.5 rounded-lg border border-dashed border-slate-200 bg-slate-50 py-3 text-[11px] text-slate-400">
              <ImageOffIcon size={13} />
              No preview available
            </div>
          )}
        </div>
      )}
    </div>
  );
}
