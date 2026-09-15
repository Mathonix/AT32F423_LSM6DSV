import type { ReactNode } from "react";
import { Card } from "@/components/ui/card";
import { cn } from "@/lib/utils";

export function MetricCard({ label, value, unit, icon, trend, className }: { label: string; value: string | number; unit?: string; icon?: ReactNode; trend?: ReactNode; className?: string }) {
  return (
    <Card className={cn("min-w-0 p-3.5", className)}>
      <div className="flex items-center justify-between gap-2 text-xs text-muted-foreground">
        <span>{label}</span>
        <span className="text-muted-foreground/70">{icon}</span>
      </div>
      <div className="mt-2 flex min-w-0 items-baseline gap-1.5">
        <span className="truncate font-mono text-[22px] font-semibold tracking-[-0.04em] text-foreground">{value}</span>
        {unit && <span className="text-[11px] text-muted-foreground">{unit}</span>}
      </div>
      {trend && <div className="mt-2 text-[11px] text-muted-foreground">{trend}</div>}
    </Card>
  );
}
