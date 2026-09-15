import { useEffect, useMemo, useRef, useState } from "react";
import {
  Activity,
  Boxes,
  Cable,
  Cpu,
  Gauge,
  GitBranch,
  Search,
  Settings,
  SlidersHorizontal,
  Unplug,
  Wrench,
} from "lucide-react";
import type { LucideIcon } from "lucide-react";
import { useDevice } from "@/context/device-context";
import type { PageId } from "@/types/device";
import { cn } from "@/lib/utils";

const destinations: Array<{ id: PageId; label: string; icon: LucideIcon }> = [
  { id: "overview", label: "Go to Overview", icon: Gauge },
  { id: "attitude", label: "Go to Attitude", icon: Boxes },
  { id: "signals", label: "Go to Signals", icon: Activity },
  { id: "calibration", label: "Go to Calibration", icon: SlidersHorizontal },
  { id: "can", label: "Go to CAN", icon: GitBranch },
  { id: "firmware", label: "Go to Firmware", icon: Cpu },
  { id: "diagnostics", label: "Go to Diagnostics", icon: Wrench },
  { id: "settings", label: "Go to Settings", icon: Settings },
];

type Action = {
  id: string;
  label: string;
  detail?: string;
  icon: LucideIcon;
  run: () => void | Promise<void>;
};

export function CommandPalette({
  open,
  onOpenChange,
  onNavigate,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onNavigate: (page: PageId) => void;
}) {
  const { connection, connect, disconnect, settings } = useDevice();
  const [query, setQuery] = useState("");
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!open) return;
    setQuery("");
    const id = window.setTimeout(() => inputRef.current?.focus(), 0);
    return () => window.clearTimeout(id);
  }, [open]);

  const actions = useMemo<Action[]>(() => {
    const items: Action[] = destinations.map(({ id, label, icon }) => ({
      id: `nav-${id}`,
      label,
      icon,
      run: () => onNavigate(id),
    }));
    if (connection === "connected") {
      items.unshift({ id: "disconnect", label: "Disconnect device", detail: "End current transport session", icon: Unplug, run: disconnect });
    } else {
      items.unshift({
        id: "connect",
        label: "Connect device",
        detail: settings.transport === "uart" ? settings.serialPort || "Select a serial port first" : "Deterministic mock transport",
        icon: Cable,
        run: connect,
      });
    }
    return items;
  }, [connect, connection, disconnect, onNavigate, settings.serialPort, settings.transport]);

  const filtered = actions.filter((action) => `${action.label} ${action.detail ?? ""}`.toLowerCase().includes(query.toLowerCase()));

  if (!open) return null;
  return (
    <div className="fixed inset-0 z-50 grid place-items-start bg-black/35 px-4 pt-[14vh] backdrop-blur-[2px]" onMouseDown={() => onOpenChange(false)}>
      <div
        className="mx-auto w-full max-w-[620px] overflow-hidden rounded-xl border border-border bg-surface-1 shadow-2xl"
        onMouseDown={(event) => event.stopPropagation()}
        role="dialog"
        aria-modal="true"
        aria-label="Command palette"
      >
        <div className="flex h-11 items-center gap-2 border-b border-border px-3">
          <Search size={15} className="text-muted-foreground" />
          <input
            ref={inputRef}
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === "Escape") onOpenChange(false);
              if (event.key === "Enter" && filtered[0]) {
                void filtered[0].run();
                onOpenChange(false);
              }
            }}
            className="h-full min-w-0 flex-1 bg-transparent text-[13px] outline-none placeholder:text-muted-foreground"
            placeholder="Search commands…"
          />
          <kbd className="rounded border border-border bg-surface-2 px-1.5 py-0.5 text-[10px] text-muted-foreground">Esc</kbd>
        </div>
        <div className="max-h-[360px] overflow-auto p-1.5">
          {filtered.map((action, index) => {
            const Icon = action.icon;
            return (
              <button
                key={action.id}
                className={cn(
                  "flex w-full items-center gap-3 rounded-md px-2.5 py-2 text-left",
                  index === 0 ? "bg-surface-3" : "hover:bg-surface-2",
                )}
                onClick={() => {
                  void action.run();
                  onOpenChange(false);
                }}
              >
                <Icon size={15} className="shrink-0 text-muted-foreground" />
                <span className="min-w-0 flex-1">
                  <span className="block text-[12.5px] text-foreground">{action.label}</span>
                  {action.detail && <span className="block truncate text-[10px] text-muted-foreground">{action.detail}</span>}
                </span>
              </button>
            );
          })}
          {filtered.length === 0 && <div className="px-3 py-8 text-center text-xs text-muted-foreground">No matching command</div>}
        </div>
        <div className="border-t border-border px-3 py-2 text-[10px] text-muted-foreground">Only navigation and firmware-supported connection actions are exposed.</div>
      </div>
    </div>
  );
}
