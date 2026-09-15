import { LockKeyhole } from "lucide-react";

export function UnsupportedPanel({ title, detail, children }: { title: string; detail: string; children?: React.ReactNode }) {
  return (
    <div className="rounded-lg border border-border bg-surface-1 p-5">
      <div className="flex items-start gap-3">
        <div className="grid h-8 w-8 shrink-0 place-items-center rounded-md border border-warning/25 bg-warning/8 text-warning"><LockKeyhole size={15} /></div>
        <div>
          <div className="text-sm font-semibold">{title}</div>
          <p className="mt-1 max-w-3xl text-xs leading-5 text-muted-foreground">{detail}</p>
        </div>
      </div>
      {children && <div className="mt-5">{children}</div>}
    </div>
  );
}
