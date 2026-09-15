import type { ReactNode } from "react";

export function PageHeader({ title, description, actions }: { title: string; description: string; actions?: ReactNode }) {
  return (
    <div className="flex min-h-14 items-center justify-between gap-6 border-b border-border px-5 py-3">
      <div className="min-w-0">
        <h1 className="text-[15px] font-semibold tracking-[-0.01em] text-foreground">{title}</h1>
        <p className="mt-0.5 truncate text-xs text-muted-foreground">{description}</p>
      </div>
      {actions && <div className="flex shrink-0 items-center gap-2">{actions}</div>}
    </div>
  );
}
