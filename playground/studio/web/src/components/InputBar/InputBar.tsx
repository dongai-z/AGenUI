/** Bottom input bar: model selector (left), auto-growing textarea, send/stop (right). */

import { useRef, useState } from "react";
import { ModelSelector } from "./ModelSelector";
import { ReasoningToggle } from "./ReasoningToggle";
import { SendButton } from "./SendButton";
import type { Provider } from "@/types";

interface InputBarProps {
  providers: Provider[];
  active: string | null;
  isGenerating: boolean;
  onSend: (prompt: string, provider: string | null, reasoning: boolean) => void;
  onStop: () => void;
}

export function InputBar({
  providers,
  active,
  isGenerating,
  onSend,
  onStop,
}: InputBarProps) {
  const [text, setText] = useState("");
  const [provider, setProvider] = useState<string | null>(null);
  const [reasoning, setReasoning] = useState(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  const canSend = text.trim().length > 0 && !isGenerating;

  const autoResize = () => {
    const el = textareaRef.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(el.scrollHeight, 160)}px`;
  };

  const handleSend = () => {
    const prompt = text.trim();
    if (!prompt || isGenerating) return;
    onSend(prompt, provider, reasoning);
    setText("");
    requestAnimationFrame(autoResize);
  };

  return (
    <div className="shrink-0 border-t border-slate-200 bg-white px-4 py-3">
      <div className="mx-auto max-w-3xl space-y-2">
        {/* Input row: full-width auto-growing textarea. */}
        <div className="rounded-xl border border-slate-200 bg-slate-50 px-3 py-2 transition focus-within:border-brand-500 focus-within:bg-white focus-within:ring-2 focus-within:ring-brand-500/15">
          <textarea
            ref={textareaRef}
            rows={1}
            value={text}
            disabled={isGenerating}
            placeholder={
              isGenerating
                ? "Generating, please wait..."
                : "Describe the UI you want, e.g. generate a weather card showing city, temperature and condition"
            }
            onChange={(e) => {
              setText(e.target.value);
              autoResize();
            }}
            onKeyDown={(e) => {
              if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
                e.preventDefault();
                handleSend();
              }
            }}
            className="block w-full resize-none bg-transparent text-sm leading-5 text-slate-800 outline-none placeholder:text-slate-400 disabled:opacity-60"
          />
        </div>

        {/* Bottom row: model selector + reasoning switch (left), hint + send/stop (right). */}
        <div className="flex items-start justify-between gap-3">
          <div className="flex items-start gap-3">
            <ModelSelector
              providers={providers}
              active={active}
              value={provider}
              disabled={isGenerating}
              onChange={setProvider}
            />
            <ReasoningToggle
              enabled={reasoning}
              disabled={isGenerating}
              onChange={setReasoning}
            />
          </div>
          <div className="flex items-center gap-2 pt-1">
            <span className="hidden text-[11px] text-slate-400 md:block">
              Enter to send, Shift+Enter for a new line
            </span>
            <SendButton
              isGenerating={isGenerating}
              canSend={canSend}
              onSend={handleSend}
              onStop={onStop}
            />
          </div>
        </div>
      </div>
    </div>
  );
}
