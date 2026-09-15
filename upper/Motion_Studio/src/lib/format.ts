export function formatCanId(id: number, extended = false) {
  return id.toString(16).toUpperCase().padStart(extended ? 8 : 3, "0");
}

export function hexByte(value: number) {
  return (value & 0xff).toString(16).toUpperCase().padStart(2, "0");
}

export function formatTimestamp(ms: number) {
  return `${(ms / 1000).toFixed(6)} s`;
}

export function formatBytes(bytes: number) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  return `${(bytes / 1024 / 1024).toFixed(2)} MiB`;
}

export function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

export function parseCanId(value: string) {
  const cleaned = value.trim().replace(/^0x/i, "");
  const parsed = Number.parseInt(cleaned, 16);
  return Number.isFinite(parsed) ? parsed : 0;
}

export function formatClock(timestamp: number, withMs = true) {
  const d = new Date(timestamp);
  const base = d.toLocaleTimeString([], { hour12: false });
  return withMs ? `${base}.${d.getMilliseconds().toString().padStart(3, "0")}` : base;
}
