use std::{
    collections::VecDeque,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

use parking_lot::Mutex;

use crate::{
    domain::device::{
        ConnectionConfig, ConnectionState, DeviceEvent, DeviceInfo, DeviceStats, HardwareSnapshot,
        SessionRecordingSummary, TelemetrySample, TransportKind,
    },
    services::{recording::SessionRecorder, telemetry::JustFloatParser},
    transport::{MockTransport, ReplayTransport, SerialTransport, Transport, TransportError},
};

const MAX_PENDING_SAMPLES: usize = 4096;
const MAX_PENDING_EVENTS: usize = 256;
const SNAPSHOT_BATCH: usize = 512;

struct SharedState {
    connection: ConnectionState,
    transport: Option<TransportKind>,
    transport_label: String,
    info: DeviceInfo,
    stats: DeviceStats,
    samples: VecDeque<TelemetrySample>,
    events: VecDeque<DeviceEvent>,
    error: Option<String>,
    session_started: Option<Instant>,
    last_packet: Option<Instant>,
    recorder: Option<SessionRecorder>,
    last_recording: Option<SessionRecordingSummary>,
}

impl Default for SharedState {
    fn default() -> Self {
        Self {
            connection: ConnectionState::Disconnected,
            transport: None,
            transport_label: "No transport".into(),
            info: DeviceInfo::default(),
            stats: DeviceStats::default(),
            samples: VecDeque::with_capacity(MAX_PENDING_SAMPLES),
            events: VecDeque::with_capacity(MAX_PENDING_EVENTS),
            error: None,
            session_started: None,
            last_packet: None,
            recorder: None,
            last_recording: None,
        }
    }
}

struct Worker {
    stop: Arc<AtomicBool>,
    handle: JoinHandle<()>,
}

pub struct DeviceService {
    shared: Arc<Mutex<SharedState>>,
    worker: Option<Worker>,
}

impl DeviceService {
    pub fn new() -> Self {
        Self { shared: Arc::new(Mutex::new(SharedState::default())), worker: None }
    }

    pub fn connect(&mut self, config: ConnectionConfig) -> Result<DeviceInfo, TransportError> {
        self.stop_worker();
        let _ = self.stop_recording();
        self.prepare_connect();

        let transport_kind = config.transport;
        let transport: Box<dyn Transport> = match transport_kind {
            TransportKind::Uart => Box::new(SerialTransport::new(config.serial_port, config.baud_rate)),
            TransportKind::Mock => Box::new(MockTransport::new()),
            TransportKind::Replay => {
                return self.fail_connect(TransportError::InvalidConfig(
                    "use connect_replay for replay sessions".into(),
                ));
            }
        };
        if let Err(error) = self.start_worker(transport_kind, transport) {
            return self.fail_connect(error);
        }
        Ok(self.shared.lock().info.clone())
    }

    pub fn connect_replay(&mut self, samples: Vec<TelemetrySample>) -> Result<DeviceInfo, TransportError> {
        if samples.is_empty() {
            return Err(TransportError::InvalidConfig("replay contains no telemetry samples".into()));
        }
        self.stop_worker();
        let _ = self.stop_recording();
        self.prepare_connect();
        if let Err(error) = self.start_worker(TransportKind::Replay, Box::new(ReplayTransport::new(samples))) {
            return self.fail_connect(error);
        }
        Ok(self.shared.lock().info.clone())
    }

    fn prepare_connect(&mut self) {
        let mut state = self.shared.lock();
        let reconnect_increment = if matches!(
            state.connection,
            ConnectionState::Error | ConnectionState::Reconnecting
        ) { 1 } else { 0 };
        let reconnect_count = state.stats.reconnect_count.saturating_add(reconnect_increment);
        state.connection = ConnectionState::Connecting;
        state.error = None;
        state.transport = None;
        state.transport_label = "No transport".into();
        state.samples.clear();
        state.events.clear();
        state.stats = DeviceStats { reconnect_count, ..DeviceStats::default() };
        state.session_started = None;
        state.last_packet = None;
    }

    fn fail_connect<T>(&mut self, error: TransportError) -> Result<T, TransportError> {
        let mut state = self.shared.lock();
        state.connection = ConnectionState::Error;
        state.error = Some(error.to_string());
        push_event(&mut state, "error", &error.to_string());
        Err(error)
    }

    pub fn disconnect(&mut self) {
        self.stop_worker();
        let _ = self.stop_recording();
        let mut state = self.shared.lock();
        state.connection = ConnectionState::Disconnected;
        state.transport = None;
        state.transport_label = "No transport".into();
        state.session_started = None;
        state.last_packet = None;
        push_event(&mut state, "info", "Disconnected");
    }

    pub fn start_recording(&mut self, created_at: String, record_raw: bool) -> Result<(), TransportError> {
        let mut state = self.shared.lock();
        if state.connection != ConnectionState::Connected {
            return Err(TransportError::InvalidConfig("connect a device before recording".into()));
        }
        if state.recorder.is_some() {
            return Err(TransportError::InvalidConfig("a recording is already active".into()));
        }
        let recorder = SessionRecorder::create(&created_at, &state.info, &state.transport_label, record_raw)
            .map_err(|error| TransportError::Io(format!("cannot start session recording: {error}")))?;
        state.recorder = Some(recorder);
        push_event(&mut state, "success", "Session recording started");
        Ok(())
    }

    pub fn stop_recording(&mut self) -> Result<Option<SessionRecordingSummary>, TransportError> {
        let recorder = {
            let mut state = self.shared.lock();
            state.recorder.take()
        };
        let Some(recorder) = recorder else {
            return Ok(self.shared.lock().last_recording.clone());
        };
        let summary = recorder
            .finish()
            .map_err(|error| TransportError::Io(format!("cannot finalize session recording: {error}")))?;
        let mut state = self.shared.lock();
        state.last_recording = Some(summary.clone());
        push_event(&mut state, "success", &format!("Session recording saved · {} samples", summary.sample_count));
        Ok(Some(summary))
    }

    pub fn open_last_recording_folder(&self) -> Result<(), TransportError> {
        let directory = self
            .shared
            .lock()
            .last_recording
            .as_ref()
            .map(|summary| summary.directory.clone())
            .ok_or_else(|| TransportError::InvalidConfig("no completed recording is available".into()))?;
        open_directory(&directory)
    }

    pub fn snapshot(&mut self) -> HardwareSnapshot {
        let mut state = self.shared.lock();
        if let Some(started) = state.session_started {
            state.stats.session_seconds = started.elapsed().as_secs_f64();
        }
        state.stats.last_packet_age_ms = state.last_packet.map(|last| last.elapsed().as_secs_f64() * 1000.0);

        let sample_count = state.samples.len().min(SNAPSHOT_BATCH);
        let mut samples = Vec::with_capacity(sample_count);
        for _ in 0..sample_count {
            if let Some(sample) = state.samples.pop_front() { samples.push(sample); }
        }
        let mut events = Vec::with_capacity(state.events.len());
        while let Some(event) = state.events.pop_front() { events.push(event); }

        HardwareSnapshot {
            connection: state.connection,
            transport: state.transport,
            transport_label: state.transport_label.clone(),
            info: state.info.clone(),
            stats: state.stats.clone(),
            samples,
            events,
            error: state.error.clone(),
        }
    }

    fn start_worker(&mut self, kind: TransportKind, mut transport: Box<dyn Transport>) -> Result<(), TransportError> {
        transport.connect()?;
        let label = transport.label();
        let stop = Arc::new(AtomicBool::new(false));
        let stop_for_thread = stop.clone();
        let shared = self.shared.clone();

        {
            let mut state = shared.lock();
            state.connection = ConnectionState::Connected;
            state.transport = Some(kind);
            state.transport_label = label.clone();
            state.error = None;
            state.session_started = Some(Instant::now());
            state.last_packet = None;
            push_event(&mut state, "success", &format!("Connected · {label}"));
        }

        let handle = thread::Builder::new()
            .name("at32-motion-reader".into())
            .spawn(move || {
                let mut parser = JustFloatParser::new();
                let started = Instant::now();
                let mut buffer = [0u8; 8192];
                let mut rate_started = Instant::now();
                let mut rate_frames = 0u64;

                while !stop_for_thread.load(Ordering::Relaxed) {
                    match transport.read(&mut buffer) {
                        Ok(0) => thread::sleep(transport.idle_delay()),
                        Ok(count) => {
                            let host_ms = started.elapsed().as_secs_f64() * 1000.0;
                            let samples = parser.feed(&buffer[..count], host_ms);
                            let metrics = parser.metrics();
                            let mut state = shared.lock();
                            let raw_record_error = state
                                .recorder
                                .as_mut()
                                .and_then(|recorder| recorder.record_raw(&buffer[..count]).err());
                            if let Some(error) = raw_record_error {
                                state.recorder = None;
                                state.events.push_back(DeviceEvent {
                                    timestamp_ms: host_ms,
                                    level: "error".into(),
                                    message: format!("Session raw recording stopped: {error}"),
                                });
                            }
                            state.stats.bytes = state.stats.bytes.saturating_add(count as u64);
                            state.stats.parser_errors = metrics.errors;
                            state.stats.resync_bytes = metrics.resync_bytes;
                            for sample in samples {
                                let sample_record_error = state
                                    .recorder
                                    .as_mut()
                                    .and_then(|recorder| recorder.record_sample(&sample).err());
                                if let Some(error) = sample_record_error {
                                    state.recorder = None;
                                    state.events.push_back(DeviceEvent {
                                        timestamp_ms: host_ms,
                                        level: "error".into(),
                                        message: format!("Session decoded recording stopped: {error}"),
                                    });
                                }
                                state.stats.frames = state.stats.frames.saturating_add(1);
                                rate_frames = rate_frames.saturating_add(1);
                                state.stats.fusion_hz = sample.fusion_hz;
                                state.stats.uart_late = sample.late;
                                state.last_packet = Some(Instant::now());
                                if state.samples.len() >= MAX_PENDING_SAMPLES {
                                    state.samples.pop_front();
                                }
                                state.samples.push_back(sample);
                            }
                            let elapsed = rate_started.elapsed();
                            if elapsed >= Duration::from_millis(500) {
                                state.stats.host_output_hz = (rate_frames as f64 / elapsed.as_secs_f64()) as f32;
                                rate_frames = 0;
                                rate_started = Instant::now();
                            }
                        }
                        Err(error) => {
                            let mut state = shared.lock();
                            state.connection = ConnectionState::Error;
                            state.error = Some(error.to_string());
                            push_event(&mut state, "error", &error.to_string());
                            break;
                        }
                    }
                }
                transport.disconnect();
            })
            .map_err(|error| TransportError::Io(error.to_string()))?;

        self.worker = Some(Worker { stop, handle });
        Ok(())
    }

    fn stop_worker(&mut self) {
        if let Some(worker) = self.worker.take() {
            worker.stop.store(true, Ordering::Relaxed);
            let _ = worker.handle.join();
        }
    }
}

impl Drop for DeviceService {
    fn drop(&mut self) {
        self.stop_worker();
        let _ = self.stop_recording();
    }
}

fn open_directory(directory: &str) -> Result<(), TransportError> {
    #[cfg(target_os = "windows")]
    let mut command = std::process::Command::new("explorer.exe");
    #[cfg(target_os = "macos")]
    let mut command = std::process::Command::new("open");
    #[cfg(all(unix, not(target_os = "macos")))]
    let mut command = std::process::Command::new("xdg-open");

    command
        .arg(directory)
        .spawn()
        .map(|_| ())
        .map_err(|error| TransportError::Io(format!("cannot open recording folder: {error}")))
}

fn push_event(state: &mut SharedState, level: &str, message: &str) {
    if state.events.len() >= MAX_PENDING_EVENTS { state.events.pop_front(); }
    let timestamp_ms = state.session_started.map(|start| start.elapsed().as_secs_f64() * 1000.0).unwrap_or(0.0);
    let event = DeviceEvent { timestamp_ms, level: level.into(), message: message.into() };
    if let Some(recorder) = state.recorder.as_mut() {
        let _ = recorder.record_event(&event);
    }
    state.events.push_back(event);
}
