import { Activity, Cpu, Gauge, Radio, TimerReset, TriangleAlert } from "lucide-react";
import type { EChartsOption } from "echarts";
import { PageHeader } from "@/components/common/page-header";
import { Badge } from "@/components/ui/badge";
import { EChart } from "@/components/charts/echart";
import { useDevice } from "@/context/device-context";

function Metric({ label, value, unit, icon: Icon }: { label: string; value: string; unit?: string; icon: typeof Activity }) {
  return <div className="min-w-0 border-r border-border px-4 last:border-r-0"><div className="flex items-center gap-1.5 text-[10px] uppercase tracking-[0.08em] text-muted-foreground"><Icon size={11} />{label}</div><div className="mt-1 font-mono text-[20px] font-medium tabular-nums">{value}<span className="ml-1 text-[10px] font-normal text-muted-foreground">{unit}</span></div></div>;
}

export function OverviewPage() {
  const { connection, info, stats, latest, history } = useDevice();
  const recent = history.slice(-300);
  const chart: EChartsOption = {
    animation: false,
    xAxis: { type: "value", min: "dataMin", max: "dataMax", axisLabel: { formatter: (v: number) => `${(v / 1000).toFixed(1)}s` } },
    yAxis: { type: "value", name: "deg" },
    legend: { data: ["Roll", "Pitch", "Yaw"], top: 0, right: 0, textStyle: { fontSize: 10 } },
    series: [
      { name: "Roll", type: "line", showSymbol: false, data: recent.map((s) => [s.timestampMs, s.roll]) },
      { name: "Pitch", type: "line", showSymbol: false, data: recent.map((s) => [s.timestampMs, s.pitch]) },
      { name: "Yaw", type: "line", showSymbol: false, data: recent.map((s) => [s.timestampMs, s.yaw]) },
    ],
  };
  return <div className="min-h-full"><PageHeader title="Overview" description="Live health and motion summary from the audited UART telemetry path." /><div className="p-5"><div className="overflow-hidden rounded-lg border border-border bg-surface-1"><div className="flex items-center justify-between border-b border-border px-4 py-3"><div><div className="text-sm font-semibold">{info.mcu} + {info.sensor}</div><div className="mt-0.5 text-[11px] text-muted-foreground">{info.protocol}</div></div><div className="flex items-center gap-2"><Badge variant={connection === "connected" ? "success" : "neutral"}>{connection}</Badge><Badge variant="accent">UART verified path</Badge></div></div><div className="grid grid-cols-5 py-4"><Metric label="Output" value={stats.hostOutputHz.toFixed(0)} unit="Hz" icon={Radio} /><Metric label="Fusion" value={stats.fusionHz.toFixed(0)} unit="Hz" icon={Gauge} /><Metric label="VQF" value={latest ? latest.vqfUs.toFixed(1) : "—"} unit="µs" icon={Cpu} /><Metric label="UART late" value={stats.uartLate.toFixed(0)} icon={TimerReset} /><Metric label="Parser" value={stats.parserErrors.toString()} unit="errors" icon={TriangleAlert} /></div></div><div className="mt-4 rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">Attitude history</div><div className="p-2"><EChart option={chart} className="h-[320px] w-full" /></div></div></div></div>;
}
