import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const required = [
  "src/App.tsx",
  "src/context/device-context.tsx",
  "src/components/layout/command-palette.tsx",
  "src/components/viewer/imu-viewer.tsx",
  "src/pages/overview.tsx",
  "src/pages/attitude.tsx",
  "src/pages/signals.tsx",
  "src/pages/calibration.tsx",
  "src/pages/can.tsx",
  "src/pages/firmware.tsx",
  "src/pages/diagnostics.tsx",
  "src/pages/settings.tsx",
  "src-tauri/src/transport/mod.rs",
  "src-tauri/src/transport/serial.rs",
  "src-tauri/src/transport/mock.rs",
  "src-tauri/src/transport/replay.rs",
  "src-tauri/src/services/device_service.rs",
  "src-tauri/src/services/recording.rs",
  "src-tauri/src/services/telemetry.rs",
  "fixtures/sample-session.json",
  "PROTOCOL.md",
  "HARDWARE_INTEGRATION.md",
  "VALIDATION.md",
];

const failures = [];
for (const relative of required) {
  const file = path.join(root, relative);
  if (!fs.existsSync(file) || fs.statSync(file).size === 0) failures.push(`missing/empty: ${relative}`);
}

const jsonFiles = [
  "package.json",
  "tsconfig.json",
  "tsconfig.app.json",
  "tsconfig.node.json",
  "src-tauri/tauri.conf.json",
  "src-tauri/capabilities/default.json",
  "fixtures/sample-session.json",
];
for (const relative of jsonFiles) {
  try { JSON.parse(fs.readFileSync(path.join(root, relative), "utf8")); }
  catch (error) { failures.push(`invalid JSON: ${relative}: ${error}`); }
}

const app = fs.readFileSync(path.join(root, "src/App.tsx"), "utf8");
for (const route of ["overview", "attitude", "signals", "calibration", "can", "firmware", "diagnostics", "settings"]) {
  if (!app.includes(`${route}:`)) failures.push(`missing route: ${route}`);
}

const telemetry = fs.readFileSync(path.join(root, "src-tauri/src/services/telemetry.rs"), "utf8");
for (const requiredText of [
  "const CHANNELS: usize = 16",
  "const FRAME_BYTES: usize = PAYLOAD_BYTES + 4",
  "[0x00, 0x00, 0x80, 0x7F]",
  "handles_fragmented_frame",
  "resynchronizes_after_garbage",
  "rejects_nan_payload",
]) {
  if (!telemetry.includes(requiredText)) failures.push(`telemetry parser missing: ${requiredText}`);
}

const protocol = fs.readFileSync(path.join(root, "PROTOCOL.md"), "utf8");
for (const forbidden of ["AA 55", "A5 5A", "PCANBasic", "CAN bridge", "CRC16_LE", "CRC32_LE"]) {
  if (protocol.includes(forbidden)) failures.push(`fabricated/obsolete protocol marker in PROTOCOL.md: ${forbidden}`);
}

const sourceRoots = ["src", "src-tauri/src"];
for (const sourceRoot of sourceRoots) {
  const stack = [path.join(root, sourceRoot)];
  while (stack.length) {
    const current = stack.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const file = path.join(current, entry.name);
      if (entry.isDirectory()) stack.push(file);
      else if (/\.(ts|tsx|rs)$/.test(entry.name)) {
        const text = fs.readFileSync(file, "utf8");
        if (/\bTODO\b|Fake Data|dead button/i.test(text)) failures.push(`unfinished marker in ${path.relative(root, file)}`);
      }
    }
  }
}

if (failures.length) {
  console.error("AT32 Motion Studio source validation FAILED");
  for (const failure of failures) console.error(` - ${failure}`);
  process.exit(1);
}
console.log(`AT32 Motion Studio source validation PASS (${required.length} required artifacts)`);
