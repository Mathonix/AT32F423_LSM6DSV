import { useEffect, useMemo, useState, type ComponentType } from "react";
import { ThemeProvider } from "@/components/theme/theme-provider";
import { DeviceProvider } from "@/context/device-context";
import { AppShell } from "@/components/layout/app-shell";
import { CommandPalette } from "@/components/layout/command-palette";
import { ErrorBoundary } from "@/components/common/error-boundary";
import { OverviewPage } from "@/pages/overview";
import { AttitudePage } from "@/pages/attitude";
import { SignalsPage } from "@/pages/signals";
import { CalibrationPage } from "@/pages/calibration";
import { CanPage } from "@/pages/can";
import { FirmwarePage } from "@/pages/firmware";
import { DiagnosticsPage } from "@/pages/diagnostics";
import { SettingsPage } from "@/pages/settings";
import type { PageId } from "@/types/device";

const pages: Record<PageId, ComponentType> = {
  overview: OverviewPage,
  attitude: AttitudePage,
  signals: SignalsPage,
  calibration: CalibrationPage,
  can: CanPage,
  firmware: FirmwarePage,
  diagnostics: DiagnosticsPage,
  settings: SettingsPage,
};

function initialPage(): PageId {
  const hash = window.location.hash.replace(/^#\/?/, "") as PageId;
  return hash in pages ? hash : "overview";
}

function Workspace() {
  const [active, setActive] = useState<PageId>(initialPage);
  const [paletteOpen, setPaletteOpen] = useState(false);
  const Page = pages[active];

  const navigate = useMemo(
    () => (page: PageId) => {
      setActive(page);
      window.history.replaceState(null, "", `#/${page}`);
    },
    [],
  );

  useEffect(() => {
    const onHash = () => setActive(initialPage());
    const onKey = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        setPaletteOpen((open) => !open);
      }
    };
    window.addEventListener("hashchange", onHash);
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("hashchange", onHash);
      window.removeEventListener("keydown", onKey);
    };
  }, []);

  return (
    <>
      <AppShell active={active} onNavigate={navigate}>
        <Page />
      </AppShell>
      <CommandPalette
        open={paletteOpen}
        onOpenChange={setPaletteOpen}
        onNavigate={navigate}
      />
    </>
  );
}

export default function App() {
  return (
    <ThemeProvider defaultTheme="dark" storageKey="at32-motion-theme">
      <ErrorBoundary>
        <DeviceProvider>
          <Workspace />
        </DeviceProvider>
      </ErrorBoundary>
    </ThemeProvider>
  );
}
