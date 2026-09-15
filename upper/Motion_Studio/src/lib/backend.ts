import { invoke } from "@tauri-apps/api/core";
import type {
  ConnectionConfig,
  DeviceInfo,
  HardwareSnapshot,
  SerialPortSummary,
  SessionRecordingSummary,
  TelemetrySample,
} from "@/types/device";

export const isTauriRuntime = () =>
  typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

export const backend = {
  listSerialPorts: async () =>
    isTauriRuntime() ? invoke<SerialPortSummary[]>("list_serial_ports") : [],
  connect: async (config: ConnectionConfig) =>
    invoke<DeviceInfo>("connect_device", { config }),
  connectReplay: async (samples: TelemetrySample[]) =>
    invoke<DeviceInfo>("connect_replay", { samples }),
  disconnect: async () => invoke<void>("disconnect_device"),
  snapshot: async () => invoke<HardwareSnapshot>("device_snapshot"),
  startRecording: async (createdAt: string, recordRaw: boolean) => invoke<void>("start_session_recording", { createdAt, recordRaw }),
  stopRecording: async () => invoke<SessionRecordingSummary | null>("stop_session_recording"),
  openLastRecordingFolder: async () => invoke<void>("open_last_recording_folder"),
};
