use tauri::State;

use crate::{
    domain::device::{
        ConnectionConfig, DeviceInfo, HardwareSnapshot, SerialPortSummary,
        SessionRecordingSummary, TelemetrySample,
    },
    AppState,
};

#[tauri::command]
pub fn list_serial_ports() -> Result<Vec<SerialPortSummary>, String> {
    serialport::available_ports()
        .map(|mut ports| {
            // Windows may enumerate Bluetooth COM ports first. Put the known
            // AT32 USB CDC device first while keeping all ports visible.
            ports.sort_by_key(|port| {
                let target = matches!(port.port_type,
                    serialport::SerialPortType::UsbPort(ref info)
                        if info.vid == 0x2E3C && info.pid == 0xF401);
                let usb = matches!(port.port_type, serialport::SerialPortType::UsbPort(_));
                (!target, !usb, port.port_name.clone())
            });
            ports.into_iter().map(|port| {
                let (port_type, vid, pid, serial_number, manufacturer, product) = match port.port_type {
                    serialport::SerialPortType::UsbPort(info) => (
                        "usb-serial".to_string(),
                        Some(info.vid),
                        Some(info.pid),
                        info.serial_number,
                        info.manufacturer,
                        info.product,
                    ),
                    serialport::SerialPortType::BluetoothPort => ("bluetooth".into(), None, None, None, None, None),
                    serialport::SerialPortType::PciPort => ("pci".into(), None, None, None, None, None),
                    serialport::SerialPortType::Unknown => ("unknown".into(), None, None, None, None, None),
                };
                SerialPortSummary {
                    name: port.port_name,
                    port_type,
                    vid,
                    pid,
                    serial_number,
                    manufacturer,
                    product,
                }
            }).collect()
        })
        .map_err(|error| error.to_string())
}

#[tauri::command]
pub fn connect_device(config: ConnectionConfig, state: State<'_, AppState>) -> Result<DeviceInfo, String> {
    state.device.lock().connect(config).map_err(|error| error.to_string())
}

#[tauri::command]
pub fn connect_replay(samples: Vec<TelemetrySample>, state: State<'_, AppState>) -> Result<DeviceInfo, String> {
    state.device.lock().connect_replay(samples).map_err(|error| error.to_string())
}

#[tauri::command]
pub fn disconnect_device(state: State<'_, AppState>) {
    state.device.lock().disconnect();
}

#[tauri::command]
pub fn device_snapshot(state: State<'_, AppState>) -> HardwareSnapshot {
    state.device.lock().snapshot()
}

#[tauri::command]
pub fn start_session_recording(created_at: String, record_raw: bool, state: State<'_, AppState>) -> Result<(), String> {
    state.device.lock().start_recording(created_at, record_raw).map_err(|error| error.to_string())
}

#[tauri::command]
pub fn stop_session_recording(state: State<'_, AppState>) -> Result<Option<SessionRecordingSummary>, String> {
    state.device.lock().stop_recording().map_err(|error| error.to_string())
}

#[tauri::command]
pub fn open_last_recording_folder(state: State<'_, AppState>) -> Result<(), String> {
    state.device.lock().open_last_recording_folder().map_err(|error| error.to_string())
}

