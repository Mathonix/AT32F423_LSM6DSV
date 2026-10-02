// 构建：src/public/* 原样复制到 dist/public（Workers Static Assets 目录），
// src/index.js 由 wrangler(esbuild) 打包到 dist/worker/index.js（与 `wrangler deploy` 上传的脚本相同）。
//   node scripts/build.mjs                   构建当前源码，并校验 Worker 脚本与线上备份逐字节一致（Worker 未改动）
//   node scripts/build.mjs --verify-original 用未修改的线上源码（backup/live-deployed 的静态资源 + src/index.js）
//                                            走同一构建流程，校验产物与线上逐字节一致
import { cpSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, copyFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { buildSync } from "esbuild";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const LIVE = join(root, "backup/live-deployed");
const require = createRequire(import.meta.url);
// Ship the renderer and its pinned dependencies from this origin, without a CDN.
if (!process.argv.includes('--verify-original')) buildSync({
  entryPoints: [join(root, 'src/pcb-view.js')], outfile: join(root, 'src/public/pcb-view.js'),
  bundle: true, minify: true, format: 'esm', target: 'es2022', legalComments: 'eof',
});
// 直接使用本工程的 JS 入口，避免 Windows 下 execFileSync 无法执行 .cmd。
// verify-original 在临时目录构建，仍使用本工程安装的同一份 Wrangler。
const WRANGLER = process.env.WRANGLER || join(dirname(require.resolve("wrangler/package.json")), "bin/wrangler.js");

function build(dir) {
  const out = join(dir, "dist");
  // Windows 的本地预览可能持有 dist 目录；在原目录更新文件即可热更新。
  mkdirSync(join(out, "public"), { recursive: true });
  cpSync(join(dir, "src/public"), join(out, "public"), { recursive: true });
  mkdirSync(join(dir, "node_modules"), { recursive: true }); // wrangler 缓存目录 node_modules/.cache
  try {
    execFileSync(process.execPath, [WRANGLER, "deploy", "--dry-run", "--outdir", "dist/worker"], { cwd: dir, stdio: ["ignore", "pipe", "pipe"] });
  } catch (error) {
    if (error.stdout) process.stderr.write(error.stdout);
    if (error.stderr) process.stderr.write(error.stderr);
    throw error;
  }
  return out;
}

function same(a, b, label) {
  const x = readFileSync(a), y = readFileSync(b);
  if (!x.equals(y)) throw new Error(`${label}: ${a} 与 ${b} 不一致（${x.length} vs ${y.length} 字节）`);
  console.log(`  ✔ ${label} 逐字节一致（${x.length} 字节）`);
}

if (process.argv.includes("--verify-original")) {
  const tmp = mkdtempSync(join(tmpdir(), "gyro-orig-"));
  try {
    mkdirSync(join(tmp, "src/public"), { recursive: true });
    copyFileSync(join(root, "src/index.js"), join(tmp, "src/index.js"));
    copyFileSync(join(root, "wrangler.jsonc"), join(tmp, "wrangler.jsonc"));
    for (const f of ["index.html", "app.js", "style.css"]) copyFileSync(join(LIVE, f), join(tmp, "src/public", f));
    const out = build(tmp);
    console.log("未修改的线上源码 → 构建产物：");
    same(join(out, "worker/index.js"), join(LIVE, "index.js"), "Worker 脚本 index.js");
    for (const f of ["index.html", "app.js", "style.css"]) same(join(out, "public", f), join(LIVE, f), `静态资源 /${f}`);
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
} else {
  const out = build(root);
  const files = readdirSync(join(out, "public"));
  console.log(`built dist/public (${files.join(", ")}) + dist/worker/index.js`);
  same(join(out, "worker/index.js"), join(LIVE, "index.js"), "Worker 脚本（未改动）与线上 33276162");
  if (!existsSync(join(out, "public/index.html"))) throw new Error("index.html missing");
}
