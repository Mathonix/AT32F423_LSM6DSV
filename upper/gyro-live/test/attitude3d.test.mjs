// 3D 姿态：欧拉角约定与固件 vqf_get_euler_deg 一致；roll=90° 不塌缩
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadCore, rng } from "./load-core.mjs";

const C = loadCore();
const D = Math.PI / 180;
// q = qz(yaw) ⊗ qy(pitch) ⊗ qx(roll)（ZYX 内旋）
function quatFromEuler(yaw, pitch, roll) {
  const [cy, sy] = [Math.cos(yaw * D / 2), Math.sin(yaw * D / 2)];
  const [cp, sp] = [Math.cos(pitch * D / 2), Math.sin(pitch * D / 2)];
  const [cr, sr] = [Math.cos(roll * D / 2), Math.sin(roll * D / 2)];
  return [cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy];
}
// 固件 vqf_wrapper.cpp vqf_get_euler_deg（逐字移植）
function fwEuler(q) {
  const sinp0 = 2 * (q[0] * q[2] - q[3] * q[1]);
  const sinp = Math.max(-1, Math.min(1, sinp0));
  return { roll: Math.atan2(2 * (q[0] * q[1] + q[2] * q[3]), 1 - 2 * (q[1] * q[1] + q[2] * q[2])) / D, pitch: Math.asin(sinp) / D, yaw: Math.atan2(2 * (q[0] * q[3] + q[1] * q[2]), 1 - 2 * (q[2] * q[2] + q[3] * q[3])) / D };
}
const close = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) < eps, `${a} vs ${b}`);

test("eulerToMatrix(ZYX) 与 quatToMatrix 一致，且固件欧拉公式可逆", () => {
  const r = rng(3);
  for (let i = 0; i < 500; i++) {
    const yaw = r() * 360 - 180, pitch = r() * 178 - 89, roll = r() * 360 - 180;
    const q = quatFromEuler(yaw, pitch, roll);
    const A = C.eulerToMatrix(yaw, pitch, roll), B = C.quatToMatrix(...q);
    for (let a = 0; a < 3; a++) for (let b = 0; b < 3; b++) close(A[a][b], B[a][b]);
    const e = fwEuler(q), e2 = C.quatToEuler(...q);
    close(e.yaw, yaw, 1e-7); close(e.pitch, pitch, 1e-7); close(e.roll, roll, 1e-7);
    close(e2.yaw, e.yaw); close(e2.pitch, e.pitch); close(e2.roll, e.roll);
  }
});

test("基本方向：yaw 绕 Z（ENU，逆时针为正），pitch 绕 Y，roll 绕 X", () => {
  const x = C.mulVec(C.eulerToMatrix(90, 0, 0), [1, 0, 0]);
  close(x[0], 0); close(x[1], 1); close(x[2], 0);
  const z = C.mulVec(C.eulerToMatrix(0, 0, 90), [0, 0, 1]);   // roll +90：机体 Z 转到世界 -Y
  close(z[0], 0); close(z[1], -1); close(z[2], 0);
  const xp = C.mulVec(C.eulerToMatrix(0, 30, 0), [1, 0, 0]);  // pitch +30：机头朝下（-Z）
  assert.ok(xp[2] < 0);
});

test("roll = ±90° / pitch = ±90° 时投影不塌缩（旧 2D 算法 roll=90° 面积为 0）", () => {
  const W = 720, H = 410;
  const base = C.projectBoard(C.eulerToMatrix(0, 0, 0), W, H).hullArea;
  assert.ok(base > 5000, `base area ${base}`);
  for (const [y, p, r] of [[0, 0, 90], [0, 0, -90], [30, 0, 90], [0, 89.9, 0], [0, -89.9, 0], [45, 20, 90]]) {
    const s = C.projectBoard(C.eulerToMatrix(y, p, r), W, H);
    assert.ok(s.hullArea > base * 0.2, `(${y},${p},${r}) area ${s.hullArea} vs base ${base}`);
    assert.ok(s.faces.length >= 1);
  }
  // roll 0 与 roll 90 的投影确实不同（真实旋转）
  const a = C.projectBoard(C.eulerToMatrix(0, 0, 0), W, H).screen, b = C.projectBoard(C.eulerToMatrix(0, 0, 90), W, H).screen;
  assert.ok(a.some((pt, i) => Math.hypot(pt[0] - b[i][0], pt[1] - b[i][1]) > 20));

  // 旧线上 draw() 的 rotate（原 489-492 行）逐字移植：roll=90° 时所有点塌缩到一条线/一点
  const legacy = (yawD, pitchD, rollD) => {
    const cx = W / 2, cy = H / 2, scale = Math.min(W, H) * 0.27;
    const yaw = yawD * D, pitch = pitchD * D, roll = rollD * D;
    return [[-1, -0.55], [1, -0.55], [1, 0.55], [-1, 0.55]].map(([px, py]) => {
      let x = px * scale; let y = py * scale * Math.cos(pitch); x *= Math.cos(roll); y *= Math.cos(roll);
      return [cx + x * Math.cos(yaw) - y * Math.sin(yaw), cy + x * Math.sin(yaw) + y * Math.cos(yaw) - Math.sin(pitch) * scale * 0.5];
    });
  };
  assert.ok(C.polygonArea(legacy(0, 0, 90)) < 1e-6, "legacy collapses at roll 90");
});
