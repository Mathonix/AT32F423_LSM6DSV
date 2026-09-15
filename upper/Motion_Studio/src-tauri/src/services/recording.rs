use std::{
    env,
    fs::{self, File},
    io::{BufWriter, Write},
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};

use crate::domain::device::{DeviceEvent, DeviceInfo, SessionRecordingSummary, TelemetrySample};

pub struct SessionRecorder {
    directory: PathBuf,
    raw: Option<BufWriter<File>>,
    csv: BufWriter<File>,
    session: BufWriter<File>,
    events: BufWriter<File>,
    metadata_path: PathBuf,
    first_sample: bool,
    sample_count: u64,
    raw_bytes: u64,
}

impl SessionRecorder {
    pub fn create(created_at: &str, info: &DeviceInfo, transport_label: &str, record_raw: bool) -> std::io::Result<Self> {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis();
        let root = env::var_os("LOCALAPPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(env::temp_dir)
            .join("AT32 Motion Studio")
            .join("Sessions");
        let directory = root.join(format!("session-{stamp}"));
        fs::create_dir_all(&directory)?;

        let raw_path = directory.join("raw.bin");
        let csv_path = directory.join("telemetry.csv");
        let session_path = directory.join("session.json");
        let events_path = directory.join("events.jsonl");
        let metadata_path = directory.join("metadata.json");

        let mut csv = BufWriter::new(File::create(&csv_path)?);
        writeln!(csv, "timestamp_ms,roll_deg,pitch_deg,yaw_deg,qw,qx,qy,qz,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g,vqf_us,fusion_hz,late")?;

        let mut session = BufWriter::new(File::create(&session_path)?);
        write!(
            session,
            "{{\"schema\":\"at32-motion-studio/session-v1\",\"createdAt\":\"{}\",\"device\":{{\"name\":\"{}\",\"mcu\":\"{}\",\"sensor\":\"{}\",\"protocol\":\"{}\",\"capabilities\":{{\"uartJustfloat\":true,\"quaternion\":true,\"gyro\":true,\"accelerometer\":true,\"hostCommands\":false,\"usbApplication\":false,\"canApplication\":false,\"magnetometer\":false,\"temperature\":false,\"hostCalibration\":false,\"bootloaderUpdate\":false}}}},\"transport\":\"{}\",\"samples\":[",
            json_escape(created_at),
            json_escape(&info.name),
            json_escape(&info.mcu),
            json_escape(&info.sensor),
            json_escape(&info.protocol),
            json_escape(transport_label),
        )?;

        let metadata = format!(
            concat!(
                "{{\n",
                "  \"schema\": \"at32-motion-studio/recording-v1\",\n",
                "  \"createdAt\": \"{}\",\n",
                "  \"device\": \"{}\",\n",
                "  \"mcu\": \"{}\",\n",
                "  \"sensor\": \"{}\",\n",
                "  \"transport\": \"{}\",\n",
                "  \"wireFormat\": \"16xf32-le + 0000807F\",\n",
                "  \"recordRawPackets\": {},\n",
                "  \"rawPacketFile\": {},\n",
                "  \"decodedCsv\": \"telemetry.csv\",\n",
                "  \"replaySession\": \"session.json\",\n",
                "  \"events\": \"events.jsonl\"\n",
                "}}\n"
            ),
            json_escape(created_at),
            json_escape(&info.name),
            json_escape(&info.mcu),
            json_escape(&info.sensor),
            json_escape(transport_label),
            record_raw,
            if record_raw { "\"raw.bin\"" } else { "null" },
        );
        fs::write(&metadata_path, metadata)?;

        Ok(Self {
            directory,
            raw: if record_raw { Some(BufWriter::new(File::create(raw_path)?)) } else { None },
            csv,
            session,
            events: BufWriter::new(File::create(events_path)?),
            metadata_path,
            first_sample: true,
            sample_count: 0,
            raw_bytes: 0,
        })
    }

    pub fn record_raw(&mut self, bytes: &[u8]) -> std::io::Result<()> {
        if let Some(raw) = self.raw.as_mut() {
            raw.write_all(bytes)?;
            self.raw_bytes = self.raw_bytes.saturating_add(bytes.len() as u64);
        }
        Ok(())
    }

    pub fn record_sample(&mut self, sample: &TelemetrySample) -> std::io::Result<()> {
        writeln!(
            self.csv,
            "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            sample.timestamp_ms,
            sample.roll,
            sample.pitch,
            sample.yaw,
            sample.quaternion[0],
            sample.quaternion[1],
            sample.quaternion[2],
            sample.quaternion[3],
            sample.gyro_dps[0],
            sample.gyro_dps[1],
            sample.gyro_dps[2],
            sample.accel_g[0],
            sample.accel_g[1],
            sample.accel_g[2],
            sample.vqf_us,
            sample.fusion_hz,
            sample.late,
        )?;

        if !self.first_sample {
            self.session.write_all(b",")?;
        }
        self.first_sample = false;
        write!(
            self.session,
            concat!(
                "{{\"timestampMs\":{},\"roll\":{},\"pitch\":{},\"yaw\":{},",
                "\"quaternion\":[{},{},{},{}],",
                "\"gyroDps\":[{},{},{}],",
                "\"accelG\":[{},{},{}],",
                "\"vqfUs\":{},\"fusionHz\":{},\"late\":{}}}"
            ),
            sample.timestamp_ms,
            sample.roll,
            sample.pitch,
            sample.yaw,
            sample.quaternion[0],
            sample.quaternion[1],
            sample.quaternion[2],
            sample.quaternion[3],
            sample.gyro_dps[0],
            sample.gyro_dps[1],
            sample.gyro_dps[2],
            sample.accel_g[0],
            sample.accel_g[1],
            sample.accel_g[2],
            sample.vqf_us,
            sample.fusion_hz,
            sample.late,
        )?;
        self.sample_count = self.sample_count.saturating_add(1);
        Ok(())
    }

    pub fn record_event(&mut self, event: &DeviceEvent) -> std::io::Result<()> {
        writeln!(
            self.events,
            "{{\"timestampMs\":{},\"level\":\"{}\",\"message\":\"{}\"}}",
            event.timestamp_ms,
            json_escape(&event.level),
            json_escape(&event.message),
        )
    }

    pub fn finish(mut self) -> std::io::Result<SessionRecordingSummary> {
        self.session.write_all(b"]}")?;
        if let Some(raw) = self.raw.as_mut() { raw.flush()?; }
        self.csv.flush()?;
        self.session.flush()?;
        self.events.flush()?;

        let directory = self.directory.to_string_lossy().to_string();
        Ok(SessionRecordingSummary {
            session_json: self.directory.join("session.json").to_string_lossy().to_string(),
            raw_binary: if self.raw.is_some() { self.directory.join("raw.bin").to_string_lossy().to_string() } else { String::new() },
            csv: self.directory.join("telemetry.csv").to_string_lossy().to_string(),
            events: self.directory.join("events.jsonl").to_string_lossy().to_string(),
            metadata: self.metadata_path.to_string_lossy().to_string(),
            directory,
            sample_count: self.sample_count,
            raw_bytes: self.raw_bytes,
        })
    }
}

fn json_escape(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
        .replace('\t', "\\t")
}
