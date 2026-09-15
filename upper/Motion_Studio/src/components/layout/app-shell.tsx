import { useState, type ReactNode } from "react";
import type { PageId } from "@/types/device";
import { Sidebar } from "./sidebar";
import { TopBar } from "./top-bar";
import { StatusBar } from "./status-bar";

export function AppShell({ active, onNavigate, children }: { active: PageId; onNavigate: (page: PageId) => void; children: ReactNode }) {
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  return (
    <div className="flex h-screen w-screen overflow-hidden bg-background text-foreground">
      <Sidebar active={active} onNavigate={onNavigate} collapsed={sidebarCollapsed} onCollapsedChange={setSidebarCollapsed} />
      <div className="flex min-w-0 flex-1 flex-col">
        <TopBar />
        <main className="min-h-0 flex-1 overflow-auto bg-background">{children}</main>
        <StatusBar />
      </div>
    </div>
  );
}
