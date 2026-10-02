// Test-only Web Serial-shaped transport. Every byte comes from a real COM port.
// Native browser permission UI and USB driver integration are NOT tested here.
(() => {
  const base = 'http://127.0.0.1:8800';
  const call = async (path, body) => {
    const r = await fetch(base + path, body === undefined ? {} : { method: 'POST', body });
    if (!r.ok) throw new Error(await r.text());
    return new Uint8Array(await r.arrayBuffer());
  };
  class RealTestPort {
    readable = null; writable = null; active = false;
    async open() {
      await call('/open', new Uint8Array()); this.active = true;
      this.readable = new ReadableStream({
        pull: async (controller) => {
          try {
            // Keep a pending read alive when telemetry is disabled. Returning
            // an empty pull would otherwise stall before a later command ACK.
            while (this.active) {
              const b = await call('/read');
              if (this.active && b.length) { controller.enqueue(b); return; }
            }
          }
          catch (e) { if (this.active) controller.error(e); }
        }, cancel: () => { this.active = false; },
      });
      this.writable = new WritableStream({ write: (bytes) => call('/write', bytes) });
    }
    async close() { this.active = false; await call('/close', new Uint8Array()); this.readable = this.writable = null; }
    getInfo() { return { usbVendorId: 0x2e3c, usbProductId: 0xf401 }; }
  }
  const port = new RealTestPort();
  Object.defineProperty(Navigator.prototype, 'serial', { configurable: true, get: () => ({
    requestPort: async () => port, getPorts: async () => [port], addEventListener() {},
  }) });
})();
