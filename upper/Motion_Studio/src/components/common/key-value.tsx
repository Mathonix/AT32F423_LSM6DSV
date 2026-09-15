import type { ReactNode } from "react";
import { cn } from "@/lib/utils";

export function KeyValue({ label, value, mono = false, children }: { label: string; value?: ReactNode; mono?: boolean; children?: ReactNode }) {
  return (
    <div className="flex min-h-8 items-center justify-between gap-4 border-b border-border/70 py-1.5 last:border-b-0">
      <span className="text-xs text-muted-foreground">{label}</span>
      <span className={cn("text-right text-xs text-foreground", mono && "font-mono")}>{children ?? value}</span>
    </div>
  );
}
