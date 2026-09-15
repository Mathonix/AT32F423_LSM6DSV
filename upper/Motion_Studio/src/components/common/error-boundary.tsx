import { Component, type ErrorInfo, type ReactNode } from "react";
import { TriangleAlert } from "lucide-react";
import { Button } from "@/components/ui/button";

type State = { error?: Error };

export class ErrorBoundary extends Component<{ children: ReactNode }, State> {
  state: State = {};

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error("AT32 Motion Studio UI error", error, info.componentStack);
  }

  render() {
    if (!this.state.error) return this.props.children;
    return (
      <div className="grid h-screen place-items-center bg-background p-6 text-foreground">
        <div className="w-full max-w-xl rounded-lg border border-danger/40 bg-surface-1 p-5">
          <div className="flex items-center gap-2 text-sm font-semibold"><TriangleAlert size={17} className="text-danger" />Workspace error</div>
          <p className="mt-2 text-xs leading-5 text-muted-foreground">The UI encountered an unexpected error. Device I/O runs in the Rust backend and is isolated from this render failure.</p>
          <pre className="mt-3 max-h-40 overflow-auto rounded-md border border-border bg-surface-0 p-3 text-[11px] text-danger">{this.state.error.message}</pre>
          <Button className="mt-4" size="sm" onClick={() => window.location.reload()}>Reload workspace</Button>
        </div>
      </div>
    );
  }
}
