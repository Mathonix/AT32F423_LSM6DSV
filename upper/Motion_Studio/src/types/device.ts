export type ConnectionState =
  | "disconnected"
  | "discovering"
  | "connecting"
  | "connected"
  | "reconnecting"
  | "error";

export type TransportKind = "uart" | "mock" | "replay";

export interface ConnectionConfig {
  transport: TransportKind;
  serialPort: string;
  baudRate: number;
}

export interface SerialPortSummary {
  name: string;
  portType: string;
  vid?: number;
  pid?: number;
  serialNumber?: string;
  manufacturer?: string;
  product?: string;
}

export interface DeviceCapabilities {
  uartJustfloat: boolean;
  quaternion: boolean;
  gyro: boolean;
  accelerometer: boolean;
  hostCommands: boolean;
  usbApplication: boolean;
  canApplication: boolean;
  magnetometer: boolean;
  temperature: boolean;
  hostCalibration: boolean;
  bootloaderUpdate: boolean;
}

export interface DeviceInfo {
  name: string;
  mcu: string;
  sensor: string;
  firmwareVersion?: string;
  protocol: string;
  capabilities: DeviceCapabilities;
}

export interface TelemetrySample {
  timestampMs: number;
  roll: number;
  pitch: number;
  yaw: number;
  quaternion: [number, number, number, number];
  gyroDps: [number, number, number];
  accelG: [number, number, number];
  vqfUs: number;
  fusionHz: number;
  late: number;
}

export interface DeviceStats {
  frames: number;
  bytes: number;
  parserErrors: number;
  resyncBytes: number;
  hostOutputHz: number;
  fusionHz: number;
  uartLate: number;
  reconnectCount: number;
  sessionSeconds: number;
  lastPacketAgeMs?: number;
}

export interface DeviceEvent {
  timestampMs: number;
  level: string;
  message: string;
}

export interface HardwareSnapshot {
  connection: ConnectionState;
  transport?: TransportKind;
  transportLabel: string;
  info: DeviceInfo;
  stats: DeviceStats;
  samples: TelemetrySample[];
  events: DeviceEvent[];
  error?: string;
}

export interface AppSettings extends ConnectionConfig {
  chartSeconds: number;
  maxHistory: number;
  uiRefreshMs: number;
  autoReconnect: boolean;
  recordRawPackets: boolean;
  fastStart: boolean;
  fusionMode: "six-axis" | "nine-axis";
  streamProtocol: "justfloat" | "binary-attitude" | "binary-imu";
  outputHz: number;
  canBitrate: number;
  lightMode: "breathing" | "solid" | "off";
}

export interface RecordedSession {
  schema: "at32-motion-studio/session-v1";
  createdAt: string;
  device: DeviceInfo;
  transport: string;
  samples: TelemetrySample[];
}

export type PageId =
  | "overview"
  | "attitude"
  | "signals"
  | "calibration"
  | "can"
  | "firmware"
  | "diagnostics"
  | "settings";

export interface SessionRecordingSummary {
  directory: string;
  sessionJson: string;
  rawBinary: string;
  csv: string;
  events: string;
  metadata: string;
  sampleCount: number;
  rawBytes: number;
}
