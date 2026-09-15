import { useMemo, useState } from "react";
import type { EChartsOption } from "echarts";
import { Download, FileJson, FolderOpen, Pause, Play, Trash2 } from "lucide-react";
import { EChart } from "@/components/charts/echart";
import { PageHeader } from "@/components/common/page-header";
import { Button } from "@/components/ui/button";
import { useDevice } from "@/context/device-context";

const AXES = ["X", "Y", "Z"] as const;

export function SignalsPage() {
  const { history, clearHistory, recording, recordedCount, lastRecording, startRecording, stopRecording, exportRecording, openRecordingFolder, settings } = useDevice();
  const [paused, setPaused] = useState(false);
  const [snapshot, setSnapshot] = useState(history);
  const [autoScale, setAutoScale] = useState(true);
  const [manualMin, setManualMin] = useState(-10);
  const [manualMax, setManualMax] = useState(10);
  const shown = paused ? snapshot : history;
  const latestTime = shown.at(-1)?.timestampMs ?? 0;
  const minTime = latestTime - settings.chartSeconds * 1000;
  const windowed = useMemo(() => shown.filter((sample) => sample.timestampMs >= minTime), [shown, minTime]);

  const optionFor = (kind: "gyro" | "accel"): EChartsOption => ({
    animation: false,
    tooltip: { trigger: "axis" as const, axisPointer: { type: "cross" as const } },
    xAxis: {
      type: "value" as const,
      min: minTime,
      max: latestTime,
      axisPointer: { show: true },
      axisLabel: { formatter: (v: number) => `${((v - latestTime) / 1000).toFixed(1)}s` },
    },
    yAxis: {
      type: "value" as const,
      name: kind === "gyro" ? "dps" : "g",
      scale: autoScale,
      min: autoScale ? undefined : manualMin,
      max: autoScale ? undefined : manualMax,
    },
    legend: { data: [...AXES], right: 4, top: 0, textStyle: { fontSize: 10 }, selectedMode: true },
    dataZoom: [{ type: "inside" as const, xAxisIndex: 0, zoomOnMouseWheel: true, moveOnMouseMove: true }],
    series: AXES.map((name, axis) => ({
      name,
      type: "line" as const,
      showSymbol: false,
      sampling: "lttb" as const,
      data: windowed.map((sample) => [sample.timestampMs, kind === "gyro" ? sample.gyroDps[axis] : sample.accelG[axis]]),
    })),
  });

  return (
    <div className="min-h-full">
      <PageHeader
        title="Signals"
        description="Batched high-rate signal viewer. Chart rendering is decoupled from the Rust receive/parser worker."
        actions={<>
          <Button size="sm" variant="secondary" onClick={() => { if (!paused) setSnapshot(history); setPaused(!paused); }}>{paused ? <Play size={13} /> : <Pause size={13} />}{paused ? "Resume" : "Pause"}</Button>
          <Button size="sm" variant="ghost" onClick={clearHistory}><Trash2 size={13} />Clear</Button>
          {recording ? <Button size="sm" variant="destructive" onClick={stopRecording}>Stop REC 路 {recordedCount.toLocaleString()}</Button> : <Button size="sm" variant="secondary" onClick={startRecording}>Start recording</Button>}
          <Button size="sm" variant="ghost" disabled={!recordedCount} onClick={() => exportRecording("csv")}><Download size={13} />CSV</Button>
          <Button size="sm" variant="ghost" disabled={!recordedCount} onClick={() => exportRecording("json")}><FileJson size={13} />JSON</Button>
          {lastRecording && <Button size="sm" variant="ghost" title={lastRecording.directory} onClick={() => void openRecordingFolder()}><FolderOpen size={13} />Session files</Button>}
        </>}
      />
      <div className="space-y-4 p-5">
        <div className="flex h-9 items-center gap-3 rounded-lg border border-border bg-surface-1 px-3 text-[11px]">
          <label className="flex items-center gap-2"><input type="checkbox" checked={autoScale} onChange={(event) => setAutoScale(event.target.checked)} />Auto scale</label>
          <div className="h-4 w-px bg-border" />
          <label className="flex items-center gap-1.5 text-muted-foreground">Y min<input type="number" value={manualMin} disabled={autoScale} onChange={(event) => setManualMin(Number(event.target.value))} className="h-6 w-20 rounded border border-border bg-input px-1.5 font-mono text-foreground disabled:opacity-50" /></label>
          <label className="flex items-center gap-1.5 text-muted-foreground">Y max<input type="number" value={manualMax} disabled={autoScale} onChange={(event) => setManualMax(Number(event.target.value))} className="h-6 w-20 rounded border border-border bg-input px-1.5 font-mono text-foreground disabled:opacity-50" /></label>
          <div className="ml-auto text-muted-foreground">Legend toggles channels 路 wheel zooms 路 drag pans 路 cursor shows exact values</div>
        </div>
        <section className="rounded-lg border border-border bg-surface-1">
          <div className="flex items-center justify-between border-b border-border px-4 py-2.5"><span className="text-xs font-medium">Gyroscope XYZ</span><span className="text-[10px] text-muted-foreground">卤2000 dps firmware range 路 displayed values are transmitted dps</span></div>
          <div className="p-2"><EChart option={optionFor("gyro")} className="h-[270px] w-full" /></div>
        </section>
        <section className="rounded-lg border border-border bg-surface-1">
          <div className="flex items-center justify-between border-b border-border px-4 py-2.5"><span className="text-xs font-medium">Acceleration XYZ</span><span className="text-[10px] text-muted-foreground">卤4 g firmware range 路 displayed values are transmitted g</span></div>
          <div className="p-2"><EChart option={optionFor("accel")} className="h-[270px] w-full" /></div>
        </section>
      </div>
    </div>
  );
}


