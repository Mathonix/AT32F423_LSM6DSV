use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum TransportKind {
    Uart,
    Mock,
    Replay,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ConnectionState {
    Disconnected,
    Discovering,
    Connecting,
    Connected,
    Reconnecting,
    Error,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ConnectionConfig {
    pub transport: TransportKind,
    pub serial_port: String,
    pub baud_rate: u32,
}

impl Default for ConnectionConfig {
    fn default() -> Self {
        Self { transport: TransportKind::Uart, serial_port: String::new(), baud_rate: 2_000_000 }
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SerialPortSummary {
    pub name: String,
    pub port_type: String,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub serial_number: Option<String>,
    pub manufacturer: Option<String>,
    pub product: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceCapabilities {
    pub uart_justfloat: bool,
    pub quaternion: bool,
    pub gyro: bool,
    pub accelerometer: bool,
    pub host_commands: bool,
    pub usb_application: bool,
    pub can_application: bool,
    pub magnetometer: bool,
    pub temperature: bool,
    pub host_calibration: bool,
    pub bootloader_update: bool,
}

impl Default for DeviceCapabilities {
    fn default() -> Self {
        Self {
            uart_justfloat: true,
            quaternion: true,
            gyro: true,
            accelerometer: true,
            host_commands: false,
            usb_application: false,
            can_application: false,
            magnetometer: false,
            temperature: false,
            host_calibration: false,
            bootloader_update: false,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceInfo {
    pub name: String,
    pub mcu: String,
    pub sensor: String,
    pub firmware_version: Option<String>,
    pub protocol: String,
    pub capabilities: DeviceCapabilities,
}

impl Default for DeviceInfo {
    fn default() -> Self {
        Self {
            name: "AT32 Motion Device".into(),
            mcu: "AT32F423KCU7".into(),
            sensor: "LSM6DSV".into(),
            firmware_version: None,
            protocol: "USART4 · VOFA JustFloat · 16ch".into(),
            capabilities: DeviceCapabilities::default(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TelemetrySample {
    pub timestamp_ms: f64,
    pub roll: f32,
    pub pitch: f32,
    pub yaw: f32,
    pub quaternion: [f32; 4],
    pub gyro_dps: [f32; 3],
    pub accel_g: [f32; 3],
    pub vqf_us: f32,
    pub fusion_hz: f32,
    pub late: f32,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceStats {
    pub frames: u64,
    pub bytes: u64,
    pub parser_errors: u64,
    pub resync_bytes: u64,
    pub host_output_hz: f32,
    pub fusion_hz: f32,
    pub uart_late: f32,
    pub reconnect_count: u32,
    pub session_seconds: f64,
    pub last_packet_age_ms: Option<f64>,
}

impl Default for DeviceStats {
    fn default() -> Self {
        Self {
            frames: 0,
            bytes: 0,
            parser_errors: 0,
            resync_bytes: 0,
            host_output_hz: 0.0,
            fusion_hz: 0.0,
            uart_late: 0.0,
            reconnect_count: 0,
            session_seconds: 0.0,
            last_packet_age_ms: None,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceEvent {
    pub timestamp_ms: f64,
    pub level: String,
    pub message: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HardwareSnapshot {
    pub connection: ConnectionState,
    pub transport: Option<TransportKind>,
    pub transport_label: String,
    pub info: DeviceInfo,
    pub stats: DeviceStats,
    pub samples: Vec<TelemetrySample>,
    pub events: Vec<DeviceEvent>,
    pub error: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionRecordingSummary {
    pub directory: String,
    pub session_json: String,
    pub raw_binary: String,
    pub csv: String,
    pub events: String,
    pub metadata: String,
    pub sample_count: u64,
    pub raw_bytes: u64,
}
