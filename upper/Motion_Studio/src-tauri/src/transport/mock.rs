use std::time::{Duration, Instant};

use super::{Result, Transport};

const TAIL: [u8; 4] = [0x00, 0x00, 0x80, 0x7F];

pub struct MockTransport {
    connected: bool,
    started: Instant,
    last_emit: Instant,
    sequence: u64,
}

impl MockTransport {
    pub fn new() -> Self {
        let now = Instant::now();
        Self { connected: false, started: now, last_emit: now, sequence: 0 }
    }

    fn frame(&mut self) -> [u8; 68] {
        let t = self.started.elapsed().as_secs_f32();
        let roll = (t * 0.83).sin() * 22.0;
        let pitch = (t * 0.57).cos() * 13.0;
        let yaw = ((t * 24.0 + 180.0) % 360.0) - 180.0;
        let (sr, cr) = ((roll.to_radians() * 0.5).sin(), (roll.to_radians() * 0.5).cos());
        let (sp, cp) = ((pitch.to_radians() * 0.5).sin(), (pitch.to_radians() * 0.5).cos());
        let (sy, cy) = ((yaw.to_radians() * 0.5).sin(), (yaw.to_radians() * 0.5).cos());
        let qw = cr * cp * cy + sr * sp * sy;
        let qx = sr * cp * cy - cr * sp * sy;
        let qy = cr * sp * cy + sr * cp * sy;
        let qz = cr * cp * sy - sr * sp * cy;
        let channels = [
            roll, pitch, yaw, qw, qx, qy, qz,
            (t * 0.83).cos() * 18.0,
            -(t * 0.57).sin() * 8.0,
            24.0 + (t * 0.2).sin(),
            (t * 0.57).sin() * 0.18,
            (t * 0.83).sin() * 0.24,
            1.0 + (t * 0.4).cos() * 0.015,
            31.0 + (t * 1.7).sin() * 2.0,
            1998.0 + (t * 0.7).sin() * 3.0,
            0.0,
        ];
        let mut frame = [0u8; 68];
        for (index, value) in channels.into_iter().enumerate() {
            frame[index * 4..index * 4 + 4].copy_from_slice(&value.to_le_bytes());
        }
        frame[64..68].copy_from_slice(&TAIL);
        self.sequence = self.sequence.wrapping_add(1);
        frame
    }
}

impl Transport for MockTransport {
    fn connect(&mut self) -> Result<()> {
        let now = Instant::now();
        self.connected = true;
        self.started = now;
        self.last_emit = now;
        Ok(())
    }

    fn disconnect(&mut self) { self.connected = false; }

    fn read(&mut self, buffer: &mut [u8]) -> Result<usize> {
        if !self.connected { return Ok(0); }
        if self.last_emit.elapsed() < Duration::from_micros(1000) { return Ok(0); }
        self.last_emit = Instant::now();
        let frame = self.frame();
        let count = frame.len().min(buffer.len());
        buffer[..count].copy_from_slice(&frame[..count]);
        Ok(count)
    }

    fn label(&self) -> String { "Deterministic mock · JustFloat 1 kHz".into() }
    fn idle_delay(&self) -> Duration { Duration::from_micros(250) }
}
