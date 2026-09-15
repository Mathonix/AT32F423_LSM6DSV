import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const hostRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const firmwareRoot = path.resolve(hostRoot, "..");
const main = fs.readFileSync(path.join(firmwareRoot, "src/main.c"), "utf8");
const bsp = fs.readFileSync(path.join(firmwareRoot, "inc/bsp.h"), "utf8");
const logger = fs.readFileSync(path.join(firmwareRoot, "tools/vofa_1khz_log.py"), "utf8");

const checks = [
  [main.includes("#define VOFA_N_CH   16U"), "firmware VOFA channel count is 16"],
  [main.includes("{0x00U, 0x00U, 0x80U, 0x7FU}"), "firmware JustFloat trailer matches 0000807F"],
  [main.includes("ch[13] = (float)vqf_live.vqf_us"), "channel 13 is vqf_us"],
  [main.includes("ch[14] = (float)vqf_live.fusion_hz"), "channel 14 is fusion_hz"],
  [main.includes("ch[15] = late"), "channel 15 is late counter"],
  [bsp.includes("#define PRINT_UART               USART4"), "firmware host UART is USART4"],
  [bsp.includes("#define PRINT_UART_BAUDRATE      2000000U"), "firmware host UART baud is 2 Mbps"],
  [logger.includes('default=2000000'), "existing host logger agrees on 2 Mbps"],
];

const failed = checks.filter(([ok]) => !ok);
if (failed.length) {
  console.error("Firmware/host audit guard FAILED");
  for (const [, message] of failed) console.error(` - ${message}`);
  process.exit(1);
}
for (const [, message] of checks) console.log(`PASS: ${message}`);
