import { Upload } from "lucide-react";
import { PageHeader } from "@/components/common/page-header";
import { Button } from "@/components/ui/button";
import { useTheme } from "@/components/theme/theme-provider";
import { useDevice } from "@/context/device-context";

const inputClass = "h-8 w-full rounded-md border border-border bg-input px-2 outline-none focus:border-accent";

export function SettingsPage() {
  const { settings, updateSettings, loadReplay } = useDevice();
  const { theme, setTheme } = useTheme();
  return <div className="min-h-full">
    <PageHeader title="??" description="???IMU ?????????????????" />
    <div className="mx-auto max-w-5xl space-y-4 p-5">
      <section className="rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">????</div>
        <div className="grid grid-cols-2 gap-4 p-4 text-xs md:grid-cols-4">
          <label className="space-y-1.5"><span className="text-muted-foreground">????</span><select className={inputClass} value={settings.fastStart ? "on" : "off"} onChange={e => updateSettings({ fastStart: e.target.value === "on" })}><option value="on">????????</option><option value="off">????????</option></select></label>
          <label className="space-y-1.5"><span className="text-muted-foreground">????</span><select className={inputClass} value={settings.fusionMode} onChange={e => updateSettings({ fusionMode: e.target.value as "six-axis" | "nine-axis" })}><option value="six-axis">??????+????</option><option value="nine-axis">????????</option></select></label>
          <label className="space-y-1.5"><span className="text-muted-foreground">????</span><select className={inputClass} value={settings.streamProtocol} onChange={e => updateSettings({ streamProtocol: e.target.value as typeof settings.streamProtocol })}><option value="justfloat">JustFloat????</option><option value="binary-attitude">??????</option><option value="binary-imu">??? IMU ?</option></select></label>
          <label className="space-y-1.5"><span className="text-muted-foreground">?????Hz?</span><input className={inputClass} type="number" min={10} max={2000} value={settings.outputHz} onChange={e => updateSettings({ outputHz: Number(e.target.value) })} /></label>
        </div>
        <div className="px-4 pb-4 text-[11px] text-warning">???????? JustFloat????????????????????????????????????</div>
      </section>
      <section className="rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">????</div><div className="grid grid-cols-2 gap-4 p-4 text-xs"><label className="space-y-1.5"><span className="text-muted-foreground">?????</span><input className={inputClass} type="number" value={settings.baudRate} onChange={e => updateSettings({ baudRate: Number(e.target.value) })} /></label><div className="space-y-2 self-end pb-1"><label className="flex items-center gap-2"><input type="checkbox" checked={settings.autoReconnect} onChange={e => updateSettings({ autoReconnect: e.target.checked })} />????</label><label className="flex items-center gap-2"><input type="checkbox" checked={settings.recordRawPackets} onChange={e => updateSettings({ recordRawPackets: e.target.checked })} />??????</label></div></div></section>
      <section className="rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">???</div><div className="flex items-center gap-3 p-4 text-xs"><span>?????</span>{([['breathing','???'],['solid','??'],['off','??']] as const).map(([v,l]) => <Button key={v} size="sm" variant={settings.lightMode === v ? "default" : "secondary"} onClick={() => updateSettings({ lightMode: v })}>{l}</Button>)}</div></section>
      <section className="rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">????</div><div className="grid grid-cols-3 gap-4 p-4 text-xs"><label className="space-y-1.5"><span>???????</span><input className={inputClass} type="number" min={2} max={60} value={settings.chartSeconds} onChange={e => updateSettings({ chartSeconds: Number(e.target.value) })} /></label><label className="space-y-1.5"><span>?????</span><input className={inputClass} type="number" min={1000} max={60000} value={settings.maxHistory} onChange={e => updateSettings({ maxHistory: Number(e.target.value) })} /></label><label className="space-y-1.5"><span>????????</span><input className={inputClass} type="number" min={25} max={250} value={settings.uiRefreshMs} onChange={e => updateSettings({ uiRefreshMs: Number(e.target.value) })} /></label></div></section>
      <section className="rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">??</div><div className="flex gap-2 p-4">{([['dark','??'],['light','??????'],['system','????']] as const).map(([v,l]) => <Button key={v} size="sm" variant={theme === v ? 'default' : 'secondary'} onClick={() => setTheme(v)}>{l}</Button>)}</div></section>
      <section className="rounded-lg border border-border bg-surface-1"><div className="border-b border-border px-4 py-2.5 text-xs font-medium">????</div><div className="flex items-center justify-between gap-4 p-4 text-xs"><span className="text-muted-foreground">?? Motion Studio ?? JSON ??</span><label className="inline-flex"><input type="file" accept="application/json,.json" className="hidden" onChange={e => { const f=e.target.files?.[0]; if(f) void loadReplay(f); }} /><span className="inline-flex h-8 cursor-default items-center gap-1.5 rounded-md border border-border bg-surface-2 px-3 text-[13px] font-medium"><Upload size={13}/>????</span></label></div></section>
    </div>
  </div>;
}
