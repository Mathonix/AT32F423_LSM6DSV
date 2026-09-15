import { Circle, Database, Radio } from "lucide-react";
import { useDevice } from "@/context/device-context";

export function StatusBar() {
  const { connection, stats, transportLabel, recording, recordedCount } = useDevice();
  return (
    <footer className="flex h-[26px] shrink-0 items-center gap-4 border-t border-border bg-surface-0 px-3 text-[10px] text-muted-foreground">
      <span className="flex items-center gap-1.5"><Circle size={8} className={connection === "connected" ? "fill-success text-success" : "fill-muted-foreground/30"} />{connection}</span>
      <span className="flex min-w-0 items-center gap-1.5"><Radio size={11} /><span className="truncate">{transportLabel}</span></span>
      <span className="ml-auto flex items-center gap-1.5"><Database size={11} />{stats.bytes.toLocaleString()} B</span>
      <span>parser {stats.parserErrors}</span>
      <span>resync {stats.resyncBytes} B</span>
      {recording && <span className="font-medium text-danger">● REC {recordedCount.toLocaleString()}</span>}
    </footer>
  );
}
