mod commands;
mod domain;
mod services;
mod transport;

use parking_lot::Mutex;
use services::device_service::DeviceService;

pub struct AppState {
    device: Mutex<DeviceService>,
}

impl Default for AppState {
    fn default() -> Self { Self { device: Mutex::new(DeviceService::new()) } }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            commands::list_serial_ports,
            commands::connect_device,
            commands::connect_replay,
            commands::disconnect_device,
            commands::device_snapshot,
            commands::start_session_recording,
            commands::stop_session_recording,
            commands::open_last_recording_folder,
        ])
        .run(tauri::generate_context!())
        .expect("error while running AT32 Motion Studio");
}
