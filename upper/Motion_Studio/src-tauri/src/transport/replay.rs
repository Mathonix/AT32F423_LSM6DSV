use std::time::{Duration, Instant};

use crate::domain::device::TelemetrySample;
use super::{Result, Transport};

const TAIL: [u8; 4] = [0x00, 0x00, 0x80, 0x7F];

pub struct ReplayTransport {
    samples: Vec<TelemetrySample>,
    index: usize,
    connected: bool,
    started: Instant,
    first_timestamp: f64,
}

impl ReplayTransport {
    pub fn new(samples: Vec<TelemetrySample>) -> Self {
        let first_timestamp = samples.first().map(|sample| sample.timestamp_ms).unwrap_or(0.0);
        Self { samples, index: 0, connected: false, started: Instant::now(), first_timestamp }
    }

    fn encode(sample: &TelemetrySample) -> [u8; 68] {
        let channels = [
            sample.roll, sample.pitch, sample.yaw,
            sample.quaternion[0], sample.quaternion[1], sample.quaternion[2], sample.quaternion[3],
            sample.gyro_dps[0], sample.gyro_dps[1], sample.gyro_dps[2],
            sample.accel_g[0], sample.accel_g[1], sample.accel_g[2],
            sample.vqf_us, sample.fusion_hz, sample.late,
        ];
        let mut frame = [0u8; 68];
        for (index, value) in channels.into_iter().enumerate() {
            frame[index * 4..index * 4 + 4].copy_from_slice(&value.to_le_bytes());
        }
        frame[64..68].copy_from_slice(&TAIL);
        frame
    }
}

impl Transport for ReplayTransport {
    fn connect(&mut self) -> Result<()> {
        self.index = 0;
        self.started = Instant::now();
        self.connected = true;
        Ok(())
    }

    fn disconnect(&mut self) { self.connected = false; }

    fn read(&mut self, buffer: &mut [u8]) -> Result<usize> {
        if !self.connected || self.samples.is_empty() { return Ok(0); }
        if self.index >= self.samples.len() {
            self.connected = false;
            return Ok(0);
        }
        let sample = &self.samples[self.index];
        let target_ms = (sample.timestamp_ms - self.first_timestamp).max(0.0);
        if self.started.elapsed().as_secs_f64() * 1000.0 < target_ms { return Ok(0); }
        let frame = Self::encode(sample);
        let count = frame.len().min(buffer.len());
        buffer[..count].copy_from_slice(&frame[..count]);
        self.index += 1;
        Ok(count)
    }

    fn label(&self) -> String { format!("Session replay · {} samples", self.samples.len()) }
    fn idle_delay(&self) -> Duration { Duration::from_millis(1) }
}
