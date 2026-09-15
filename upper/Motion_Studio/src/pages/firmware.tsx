import { FileUp, ShieldCheck } from "lucide-react";
import { PageHeader } from "@/components/common/page-header";
import { UnsupportedPanel } from "@/components/common/unsupported-panel";
import { Button } from "@/components/ui/button";

export function FirmwarePage() {
  return (
    <div className="min-h-full">
      <PageHeader title="Firmware" description="Firmware update workflow is intentionally locked until a real bootloader protocol exists." />
      <div className="p-5">
        <UnsupportedPanel title="Bootloader / field update · Firmware not supported" detail="The audited repository contains development flashing helpers (DAP/J-Link) but no product bootloader directory or USB/UART upgrade packet protocol. Erase/program/verify commands are therefore not implemented in the Host.">
          <div className="rounded-md border border-border bg-surface-0">
            <div className="grid grid-cols-[1.2fr_1fr_1fr] gap-4 border-b border-border px-4 py-3 text-xs">
              <div><div className="text-muted-foreground">Current firmware</div><div className="mt-1">Unavailable in telemetry protocol</div></div>
              <div><div className="text-muted-foreground">Application address</div><div className="mt-1 font-mono">Not defined for Host</div></div>
              <div><div className="text-muted-foreground">Upgrade transport</div><div className="mt-1">Not defined</div></div>
            </div>
            <div className="flex items-center gap-2 px-4 py-4"><Button variant="secondary" disabled><FileUp size={13} />Choose firmware</Button><Button disabled><ShieldCheck size={13} />Validate & upgrade</Button></div>
          </div>
        </UnsupportedPanel>
      </div>
    </div>
  );
}
