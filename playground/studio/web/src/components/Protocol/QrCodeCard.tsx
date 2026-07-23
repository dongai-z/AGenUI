/** QR code card: encodes the raw protocol URL for Playground QR-scan loading. */

import { QRCodeSVG } from "qrcode.react";
import { useState } from "react";
import { CheckIcon, CopyIcon } from "@/components/icons";
import { copyText } from "@/lib/utils";

interface QrCodeCardProps {
  url: string;
}

export function QrCodeCard({ url }: QrCodeCardProps) {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    if (await copyText(url)) {
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1500);
    }
  };

  return (
    <div className="flex items-center gap-3 rounded-lg border border-slate-200 bg-white p-2.5">
      <div className="shrink-0 rounded-md border border-slate-100 bg-white p-1">
        <QRCodeSVG value={url} size={84} level="M" />
      </div>
      <div className="min-w-0 flex-1">
        <p className="text-[11px] font-medium text-slate-600">
          Scan with AGenUI Playground
        </p>
        <p className="mt-0.5 break-all font-mono text-[10px] leading-4 text-slate-400">
          {url}
        </p>
        <button
          type="button"
          onClick={handleCopy}
          className="mt-1.5 inline-flex items-center gap-1 rounded-md border border-slate-200 px-2 py-1 text-[11px] font-medium text-slate-600 transition hover:border-slate-300 hover:bg-slate-50"
        >
          {copied ? (
            <>
              <CheckIcon size={11} className="text-emerald-500" />
              Copied
            </>
          ) : (
            <>
              <CopyIcon size={11} />
              Copy URL
            </>
          )}
        </button>
      </div>
    </div>
  );
}
