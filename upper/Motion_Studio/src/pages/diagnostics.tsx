import { Copy, Download, Trash2 } from "lucide-react";
import { PageHeader } from "@/components/common/page-header";
import { Button } from "@/components/ui/button";
import { useDevice } from "@/context/device-context";

export function DiagnosticsPage() {
  const { connection, info, stats, latest, events, transportLabel, clearEvents } = useDevice();
  const diagnostics = {
    connection,
    transport: transportLabel,
    device: info,
    stats,
    latest: latest ? { vqfUs: latest.vqfUs, fusionHz: latest.fusionHz, late: latest.late } : null,
    protocolLimitations: { whoAmI: "DAP-only", initError: "DAP-only", outputHz: "host-derived", crc: "not present in JustFloat" },
  };
  const text = JSON.stringify(diagnostics, null, 2);
  const download = () => {
    const blob = new Blob([text], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a"); a.href = url; a.download = "at32-motion-diagnostics.json"; a.click(); URL.revokeObjectURL(url);
  };

  return (
    <div className="min-h-full">
      <PageHeader title="Diagnostics" description="Transport, parser and session health without inventing fields absent from the UART protocol." actions={<><Button size="sm" variant="secondary" onClick={() => void navigator.clipboard.writeText(text)}><Copy size={13} />Copy</Button><Button size="sm" variant="ghost" onClick={download}><Download size={13} />Export</Button><Button size="sm" variant="ghost" onClick={clearEvents} disabled={!events.length}><Trash2 size={13} />Clear log</Button></>} />
      <div className="grid grid-cols-[minmax(0,1fr)_minmax(360px,0.75fr)] gap-4 p-5">
        <section className="rounded-lg border border-border bg-surface-1">
          <div className="border-b border-border px-4 py-2.5 text-xs font-medium">Session counters</div>
          <div className="grid grid-cols-2 gap-px bg-border">
            {[
              ['Transport', transportLabel],
              ['Frames', stats.frames.toLocaleString()],
              ['Bytes', stats.bytes.toLocaleString()],
              ['Host output rate', `${stats.hostOutputHz.toFixed(1)} Hz`],
              ['Firmware fusion rate', `${stats.fusionHz.toFixed(1)} Hz`],
              ['UART late counter', stats.uartLate.toFixed(0)],
              ['Parser errors', stats.parserErrors.toString()],
              ['Dropped frames', 'Unavailable · no sequence field'],
              ['Resync bytes', stats.resyncBytes.toLocaleString()],
              ['Last packet age', stats.lastPacketAgeMs == null ? '—' : `${stats.lastPacketAgeMs.toFixed(1)} ms`],
              ['Session', `${stats.sessionSeconds.toFixed(1)} s`],
              ['Reconnect count', stats.reconnectCount.toString()],
              ['WHO_AM_I', 'DAP-only · expected 0x70'],
              ['CRC errors', 'N/A · JustFloat has no CRC'],
            ].map(([label, value]) => <div key={label} className="flex items-center justify-between bg-surface-1 px-4 py-3 text-xs"><span className="text-muted-foreground">{label}</span><span className="max-w-[60%] truncate font-mono text-[11px]">{value}</span></div>)}
          </div>
        </section>
        <section className="rounded-lg border border-border bg-surface-1">
          <div className="border-b border-border px-4 py-2.5 text-xs font-medium">Connection event timeline</div>
          <div className="max-h-[520px] overflow-auto p-3">
            {!events.length && <div className="p-6 text-center text-xs text-muted-foreground">No events in this session.</div>}
            {events.map((event, index) => <div key={`${event.timestampMs}-${index}`} className="flex gap-3 border-b border-border/70 px-1 py-2.5 text-[11px] last:border-0"><span className="w-16 shrink-0 font-mono text-muted-foreground">{(event.timestampMs / 1000).toFixed(3)}s</span><span className={event.level === 'error' ? 'text-danger' : event.level === 'success' ? 'text-success' : ''}>{event.message}</span></div>)}
          </div>
        </section>
      </div>
    </div>
  );
}
