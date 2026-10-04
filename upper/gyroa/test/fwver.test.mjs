// fwver1：应用固件版本 0x23 → 0x32 解码单元测试。node --test test/fwver.test.mjs（规格 host-agent-firmware-version.md）。NOT real hardware.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, decodePayload, buildFrame };`)();
const pl = (text, { format = 1, len = text.length, size = 16 } = {}) => { const u = new Uint8Array(size); u[0] = format; u[1] = len; [...text].forEach((c, i) => { if (2 + i < size) u[2 + i] = c.charCodeAt(0); }); return u; };
test("ids", () => { assert.equal(core.CMD.QUERY_FW_VERSION, 0x23); assert.equal(core.MSG.FW_VERSION, 0x32); assert.equal(core.MSG.MOTION_BIAS, 0x09, "0x09 stays motion bias");
  const f = core.buildFrame(0x23, 3, []); assert.deepEqual([...f.slice(0, 5)], [0xaa, 0x55, 0x23, 0, 3]); });
test("valid 0x32 → text by length", () => {
  assert.deepEqual(core.decodePayload(0x32, pl("20261002b")), { type: "fwVersion", format: 1, text: "20261002b" });
  assert.equal(core.decodePayload(0x32, pl("20261003a")).text, "20261003a");
  assert.equal(core.decodePayload(0x32, pl("abcdefghijklm")).text, "abcdefghijklm", "13 chars + NUL fits char[14]");
});
test("invalid 0x32 dropped", () => {
  assert.equal(core.decodePayload(0x32, pl("20261002b", { size: 15 })).type, "badLength");
  assert.equal(core.decodePayload(0x32, pl("20261002b", { size: 17 })).type, "badLength");
  assert.equal(core.decodePayload(0x32, pl("20261002b", { format: 2 })).type, "unknown");
  assert.equal(core.decodePayload(0x32, pl("", { len: 0 })).type, "unknown");
  assert.equal(core.decodePayload(0x32, pl("abcdefghijklmn")).type, "unknown", "14 chars → no NUL");
  const u = pl("20261002b"); u[11] = 0x41; assert.equal(core.decodePayload(0x32, u).type, "unknown", "missing NUL after len");
  const w = pl("20261002b"); w[3] = 0xc3; assert.equal(core.decodePayload(0x32, w).type, "unknown", "non-ASCII");
  assert.equal(core.decodePayload(0x09, pl("20261002b")).type, "badLength", "16 B on 0x09 is not a version frame");
});
