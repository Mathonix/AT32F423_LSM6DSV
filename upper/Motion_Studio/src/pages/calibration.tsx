import { CheckCircle2, CircleDashed, Save } from "lucide-react";
import { PageHeader } from "@/components/common/page-header";
import { UnsupportedPanel } from "@/components/common/unsupported-panel";
import { Button } from "@/components/ui/button";

export function CalibrationPage() {
  return (
    <div className="min-h-full">
      <PageHeader title="Calibration" description="Calibration Center reflects only capabilities present in the audited firmware." />
      <div className="space-y-4 p-5">
        <section className="rounded-lg border border-border bg-surface-1">
          <div className="border-b border-border px-4 py-3 text-xs font-medium">Implemented in firmware · automatic startup rest calibration</div>
          <div className="grid grid-cols-4">
            {[['Prepare','Keep the board still after power-up.'],['Collect','Firmware requires one contiguous ~1 s rest window.'],['Validate','Motion resets accumulation; thresholds are enforced in firmware.'],['Apply','Bias/tilt priming is applied in RAM before the main loop.']].map(([title, detail], index) => (
              <div key={title} className="border-r border-border p-4 last:border-r-0">
                <div className="flex items-center gap-2 text-xs font-medium">{index < 4 ? <CheckCircle2 size={14} className="text-success" /> : <CircleDashed size={14} />}{index + 1}. {title}</div>
                <p className="mt-2 text-[11px] leading-5 text-muted-foreground">{detail}</p>
              </div>
            ))}
          </div>
        </section>

        <UnsupportedPanel title="Host-controlled calibration · Firmware not supported" detail="No audited UART command parser exists for gyro calibration, six-position accelerometer calibration, magnetometer calibration, calibration matrix/bias upload, or flash save. The Host will not pretend these actions can be executed.">
          <div className="grid grid-cols-3 gap-3">
            {['Gyroscope bias', 'Accelerometer 6-position', 'Magnetometer hard/soft iron'].map((name) => (
              <div key={name} className="rounded-md border border-border bg-surface-0 p-3">
                <div className="text-xs font-medium">{name}</div>
                <div className="mt-1 text-[11px] text-muted-foreground">Interface reserved · no firmware command</div>
                <Button className="mt-3" size="sm" variant="secondary" disabled><Save size={13} />Start / Save</Button>
              </div>
            ))}
          </div>
        </UnsupportedPanel>
      </div>
    </div>
  );
}
