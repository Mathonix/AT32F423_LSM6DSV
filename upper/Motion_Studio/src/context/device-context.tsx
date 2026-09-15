import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from "react";
import { backend, isTauriRuntime } from "@/lib/backend";
import type {
  AppSettings,
  ConnectionState,
  DeviceEvent,
  DeviceInfo,
  DeviceStats,
  RecordedSession,
  SerialPortSummary,
  SessionRecordingSummary,
  TelemetrySample,
} from "@/types/device";

const DEFAULT_SETTINGS: AppSettings = {
  transport: isTauriRuntime() ? "uart" : "mock",
  serialPort: "",
  baudRate: 2_000_000,
  chartSeconds: 10,
  maxHistory: 12_000,
  uiRefreshMs: 50,
  autoReconnect: true,
  recordRawPackets: true,
  fastStart: true,
  fusionMode: "nine-axis",
  streamProtocol: "justfloat",
  outputHz: 1000,
  canBitrate: 500000,
  lightMode: "breathing",
};

const DEFAULT_INFO: DeviceInfo = {
  name: "AT32 Motion Device",
  mcu: "AT32F423KCU7",
  sensor: "LSM6DSV",
  protocol: "USART4 路 VOFA JustFloat 路 16ch",
  capabilities: {
    uartJustfloat: true,
    quaternion: true,
    gyro: true,
    accelerometer: true,
    hostCommands: false,
    usbApplication: false,
    canApplication: false,
    magnetometer: false,
    temperature: false,
    hostCalibration: false,
    bootloaderUpdate: false,
  },
};

const DEFAULT_STATS: DeviceStats = {
  frames: 0,
  bytes: 0,
  parserErrors: 0,
  resyncBytes: 0,
  hostOutputHz: 0,
  fusionHz: 0,
  uartLate: 0,
  reconnectCount: 0,
  sessionSeconds: 0,
};

function loadSettings(): AppSettings {
  try {
    const saved = localStorage.getItem("at32-motion-settings");
    return saved ? { ...DEFAULT_SETTINGS, ...JSON.parse(saved) } : DEFAULT_SETTINGS;
  } catch (settingsError) {
    console.error("Failed to load AT32 Motion Studio settings; defaults will be used.", settingsError);
    return DEFAULT_SETTINGS;
  }
}

function deterministicSample(timestampMs: number): TelemetrySample {
  const t = timestampMs / 1000;
  const roll = Math.sin(t * 0.83) * 22;
  const pitch = Math.cos(t * 0.57) * 13;
  const yaw = ((t * 24 + 180) % 360) - 180;
  const r = (roll * Math.PI) / 360;
  const p = (pitch * Math.PI) / 360;
  const y = (yaw * Math.PI) / 360;
  const [sr, cr, sp, cp, sy, cy] = [Math.sin(r), Math.cos(r), Math.sin(p), Math.cos(p), Math.sin(y), Math.cos(y)];
  return {
    timestampMs,
    roll,
    pitch,
    yaw,
    quaternion: [
      cr * cp * cy + sr * sp * sy,
      sr * cp * cy - cr * sp * sy,
      cr * sp * cy + sr * cp * sy,
      cr * cp * sy - sr * sp * cy,
    ],
    gyroDps: [Math.cos(t * 0.83) * 18, -Math.sin(t * 0.57) * 8, 24 + Math.sin(t * 0.2)],
    accelG: [Math.sin(t * 0.57) * 0.18, Math.sin(t * 0.83) * 0.24, 1 + Math.cos(t * 0.4) * 0.015],
    vqfUs: 31 + Math.sin(t * 1.7) * 2,
    fusionHz: 1998 + Math.sin(t * 0.7) * 3,
    late: 0,
  };
}

type DeviceContextValue = {
  connection: ConnectionState;
  error: string;
  info: DeviceInfo;
  stats: DeviceStats;
  latest?: TelemetrySample;
  history: TelemetrySample[];
  events: DeviceEvent[];
  settings: AppSettings;
  serialPorts: SerialPortSummary[];
  transportLabel: string;
  recording: boolean;
  recordedCount: number;
  lastRecording?: SessionRecordingSummary;
  connect: () => Promise<void>;
  disconnect: () => Promise<void>;
  refreshSerialPorts: () => Promise<void>;
  updateSettings: (patch: Partial<AppSettings>) => void;
  clearHistory: () => void;
  clearEvents: () => void;
  startRecording: () => Promise<void>;
  stopRecording: () => Promise<void>;
  exportRecording: (format: "json" | "csv") => void;
  openRecordingFolder: () => Promise<void>;
  loadReplay: (file: File) => Promise<void>;
};

const DeviceContext = createContext<DeviceContextValue | null>(null);

function downloadText(filename: string, text: string, type: string) {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}

export function DeviceProvider({ children }: { children: ReactNode }) {
  const [settings, setSettings] = useState<AppSettings>(loadSettings);
  const [connection, setConnection] = useState<ConnectionState>(isTauriRuntime() ? "disconnected" : "connected");
  const [error, setError] = useState("");
  const [info, setInfo] = useState(DEFAULT_INFO);
  const [stats, setStats] = useState(DEFAULT_STATS);
  const [history, setHistory] = useState<TelemetrySample[]>([]);
  const [events, setEvents] = useState<DeviceEvent[]>([]);
  const [serialPorts, setSerialPorts] = useState<SerialPortSummary[]>([]);
  const [transportLabel, setTransportLabel] = useState(isTauriRuntime() ? "No transport" : "Browser preview 路 deterministic mock");
  const [recording, setRecording] = useState(false);
  const [recordedCount, setRecordedCount] = useState(0);
  const [lastRecording, setLastRecording] = useState<SessionRecordingSummary>();
  const recordingRef = useRef<TelemetrySample[]>([]);
  const recordingStartFrames = useRef(0);
  const desiredConnected = useRef(!isTauriRuntime());
  const reconnecting = useRef(false);
  const browserStarted = useRef(performance.now());

  const latest = history.at(-1);

  useEffect(() => {
    localStorage.setItem("at32-motion-settings", JSON.stringify(settings));
  }, [settings]);

  const appendSamples = useCallback((samples: TelemetrySample[]) => {
    if (!samples.length) return;
    setHistory((old) => {
      const combined = old.concat(samples);
      return combined.length > settings.maxHistory ? combined.slice(-settings.maxHistory) : combined;
    });
    if (recording && !isTauriRuntime()) {
      recordingRef.current.push(...samples);
      if (recordingRef.current.length > 600_000) recordingRef.current.splice(0, recordingRef.current.length - 600_000);
      setRecordedCount(recordingRef.current.length);
    }
  }, [recording, settings.maxHistory]);

  useEffect(() => {
    if (isTauriRuntime()) return;
    const timer = window.setInterval(() => {
      const end = performance.now() - browserStarted.current;
      const batch = Array.from({ length: 50 }, (_, index) => deterministicSample(end - (49 - index)));
      appendSamples(batch);
      setStats((old) => ({
        ...old,
        frames: old.frames + batch.length,
        bytes: old.bytes + batch.length * 68,
        hostOutputHz: 1000,
        fusionHz: batch.at(-1)?.fusionHz ?? 0,
        sessionSeconds: end / 1000,
        lastPacketAgeMs: 0,
      }));
    }, 50);
    return () => window.clearInterval(timer);
  }, [appendSamples]);

  useEffect(() => {
    if (!isTauriRuntime()) return;
    const timer = window.setInterval(async () => {
      try {
        const snapshot = await backend.snapshot();
        setConnection(snapshot.connection);
        setError(snapshot.error ?? "");
        setInfo(snapshot.info);
        setStats(snapshot.stats);
        setTransportLabel(snapshot.transportLabel);
        if (recording) setRecordedCount(Math.max(0, snapshot.stats.frames - recordingStartFrames.current));
        appendSamples(snapshot.samples);
        if (snapshot.events.length) setEvents((old) => [...old, ...snapshot.events].slice(-250));

        if (
          desiredConnected.current &&
          settings.autoReconnect &&
          settings.transport === "uart" &&
          snapshot.connection === "error" &&
          !reconnecting.current
        ) {
          reconnecting.current = true;
          window.setTimeout(async () => {
            try {
              setConnection("reconnecting");
              await backend.connect(settings);
            } catch (reconnectError) {
              setError(String(reconnectError));
            } finally {
              reconnecting.current = false;
            }
          }, 1500);
        }
      } catch (snapshotError) {
        setError(String(snapshotError));
      }
    }, settings.uiRefreshMs);
    return () => window.clearInterval(timer);
  }, [appendSamples, recording, settings]);

  const refreshSerialPorts = useCallback(async () => {
    if (!isTauriRuntime()) return;
    setConnection((current) => current === "connected" ? current : "discovering");
    try {
      const ports = await backend.listSerialPorts();
      // Prefer the known AT32 USB CDC identity, never Windows enumeration order.
      const target = ports.find((port) => port.vid === 0x2E3C && port.pid === 0xF401);
      const usb = ports.find((port) => port.portType === "usb-serial");
      const preferred = target ?? usb;
      setSerialPorts(ports);
      setSettings((current) => {
        if (current.serialPort && ports.some((port) => port.name === current.serialPort)) return current;
        return preferred ? { ...current, serialPort: preferred.name } : { ...current, serialPort: "" };
      });
    } catch (portError) {
      setError(`无法枚举串口：${String(portError)}`);
    } finally {
      setConnection((current) => current === "discovering" ? "disconnected" : current);
    }
  }, []);

  useEffect(() => { void refreshSerialPorts(); }, [refreshSerialPorts]);

  const connect = useCallback(async () => {
    desiredConnected.current = true;
    setError("");
    if (!isTauriRuntime()) { setConnection("connected"); return; }
    setConnection("connecting");
    try {
      const device = await backend.connect(settings);
      setInfo(device);
    } catch (connectError) {
      desiredConnected.current = false;
      setConnection("error");
      const selected = settings.serialPort || "未选择";
      setError(`连接 ${selected} 失败：${String(connectError)}。请确认选择 COM16（USB VID_2E3C PID_F401），并关闭占用串口的其他工具。`);
    }
  }, [settings]);

  const disconnect = useCallback(async () => {
    desiredConnected.current = false;
    reconnecting.current = false;
    try {
      if (isTauriRuntime() && recording) {
        const summary = await backend.stopRecording();
        if (summary) {
          setLastRecording(summary);
          setRecordedCount(summary.sampleCount);
        }
        setRecording(false);
      }
      if (isTauriRuntime()) await backend.disconnect();
      setConnection("disconnected");
    } catch (disconnectError) {
      setError(String(disconnectError));
    }
  }, [recording]);

  const updateSettings = useCallback((patch: Partial<AppSettings>) => {
    setSettings((current) => ({ ...current, ...patch }));
  }, []);

  const clearHistory = useCallback(() => setHistory([]), []);
  const clearEvents = useCallback(() => setEvents([]), []);

  const startRecording = useCallback(async () => {
    recordingRef.current = [];
    recordingStartFrames.current = stats.frames;
    setRecordedCount(0);
    setLastRecording(undefined);
    setError("");
    try {
      if (isTauriRuntime()) {
        await backend.startRecording(new Date().toISOString(), settings.recordRawPackets);
      }
      setRecording(true);
    } catch (recordingError) {
      setError(String(recordingError));
    }
  }, [settings.recordRawPackets, stats.frames]);

  const stopRecording = useCallback(async () => {
    try {
      if (isTauriRuntime()) {
        const summary = await backend.stopRecording();
        if (summary) {
          setLastRecording(summary);
          setRecordedCount(summary.sampleCount);
        }
      }
      setRecording(false);
    } catch (recordingError) {
      setError(String(recordingError));
    }
  }, []);

  const openRecordingFolder = useCallback(async () => {
    try {
      if (isTauriRuntime()) await backend.openLastRecordingFolder();
    } catch (folderError) {
      setError(String(folderError));
    }
  }, []);

  const exportRecording = useCallback((format: "json" | "csv") => {
    if (isTauriRuntime()) {
      void backend.openLastRecordingFolder();
      return;
    }
    const samples = recordingRef.current;
    if (!samples.length) return;
    const stem = `at32-motion-${new Date().toISOString().replaceAll(":", "-")}`;
    if (format === "json") {
      const session: RecordedSession = {
        schema: "at32-motion-studio/session-v1",
        createdAt: new Date().toISOString(),
        device: info,
        transport: transportLabel,
        samples,
      };
      downloadText(`${stem}.json`, JSON.stringify(session), "application/json");
      return;
    }
    const header = "timestamp_ms,roll_deg,pitch_deg,yaw_deg,qw,qx,qy,qz,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g,vqf_us,fusion_hz,late";
    const rows = samples.map((s) => [
      s.timestampMs, s.roll, s.pitch, s.yaw, ...s.quaternion, ...s.gyroDps, ...s.accelG, s.vqfUs, s.fusionHz, s.late,
    ].join(","));
    downloadText(`${stem}.csv`, [header, ...rows].join("\n"), "text/csv");
  }, [info, transportLabel]);

  const loadReplay = useCallback(async (file: File) => {
    try {
      const parsed = JSON.parse(await file.text()) as RecordedSession;
      if (parsed.schema !== "at32-motion-studio/session-v1" || !Array.isArray(parsed.samples)) {
        throw new Error("Unsupported replay file schema");
      }
      desiredConnected.current = true;
      setHistory([]);
      setError("");
      if (isTauriRuntime()) {
        await backend.connectReplay(parsed.samples);
        setConnection("connected");
      } else {
        appendSamples(parsed.samples);
        setConnection("connected");
        setTransportLabel(`Replay preview 路 ${parsed.samples.length} samples`);
      }
    } catch (replayError) {
      setError(String(replayError));
    }
  }, [appendSamples]);

  const value = useMemo<DeviceContextValue>(() => ({
    connection, error, info, stats, latest, history, events, settings, serialPorts, transportLabel,
    recording, recordedCount, lastRecording, connect, disconnect, refreshSerialPorts, updateSettings, clearHistory, clearEvents,
    startRecording, stopRecording, exportRecording, openRecordingFolder, loadReplay,
  }), [connection, error, info, stats, latest, history, events, settings, serialPorts, transportLabel, recording, recordedCount, lastRecording,
    connect, disconnect, refreshSerialPorts, updateSettings, clearHistory, clearEvents, startRecording, stopRecording, exportRecording, openRecordingFolder, loadReplay]);

  return <DeviceContext.Provider value={value}>{children}</DeviceContext.Provider>;
}

export function useDevice() {
  const context = useContext(DeviceContext);
  if (!context) throw new Error("useDevice must be used inside DeviceProvider");
  return context;
}


