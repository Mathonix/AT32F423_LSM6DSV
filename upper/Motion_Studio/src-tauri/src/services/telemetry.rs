use crate::domain::device::TelemetrySample;

// The current firmware emits VOFA+ JustFloat on USB CDC and USART4 as:
//   float yaw, float pitch, float roll, 0x00 0x00 0x80 0x7f
// That is 16 bytes per frame (12-byte payload + 4-byte tail), not the old
// 68-byte/16-channel diagnostic frame. Keep both layouts for old recordings.
const CHANNELS_16: usize = 16;
const PAYLOAD_3: usize = 3 * 4;
const PAYLOAD_16: usize = CHANNELS_16 * 4;
const TAIL: [u8; 4] = [0x00, 0x00, 0x80, 0x7F];
const MAX_BUFFER: usize = 16 * (PAYLOAD_16 + TAIL.len());

#[derive(Default, Debug, Clone, Copy)]
pub struct ParserMetrics {
    pub errors: u64,
    pub resync_bytes: u64,
}

pub struct JustFloatParser {
    buffer: Vec<u8>,
    metrics: ParserMetrics,
}

impl JustFloatParser {
    pub fn new() -> Self {
        Self { buffer: Vec::with_capacity(PAYLOAD_16 * 4), metrics: ParserMetrics::default() }
    }

    pub fn metrics(&self) -> ParserMetrics { self.metrics }

    pub fn feed(&mut self, data: &[u8], host_time_ms: f64) -> Vec<TelemetrySample> {
        self.buffer.extend_from_slice(data);
        let mut decoded = Vec::new();

        loop {
            let Some(marker) = find_subslice(&self.buffer, &TAIL) else {
                if self.buffer.len() > MAX_BUFFER {
                    let keep = PAYLOAD_3 + TAIL.len() - 1;
                    let drop = self.buffer.len().saturating_sub(keep);
                    self.buffer.drain(..drop);
                    self.metrics.resync_bytes += drop as u64;
                }
                break;
            };

            if marker < PAYLOAD_3 {
                let drain = marker + TAIL.len();
                self.buffer.drain(..drain);
                self.metrics.errors += 1;
                self.metrics.resync_bytes += drain as u64;
                continue;
            }

            // A marker at/after 64 bytes may be an old 16-channel frame. The
            // current firmware's first marker is at byte 12, so USB CDC data
            // always takes the 3-channel path below.
            let payload_len = if marker >= PAYLOAD_16 { PAYLOAD_16 } else { PAYLOAD_3 };
            let start = marker - payload_len;
            if start > 0 {
                self.metrics.resync_bytes += start as u64;
            }
            let payload = self.buffer[start..marker].to_vec();
            self.buffer.drain(..marker + TAIL.len());

            let sample = if payload_len == PAYLOAD_16 {
                decode_16(&payload, host_time_ms)
            } else {
                decode_3(&payload, host_time_ms)
            };
            match sample {
                Some(sample) => decoded.push(sample),
                None => self.metrics.errors += 1,
            }
        }
        decoded
    }
}

fn read_f32(payload: &[u8], index: usize) -> Option<f32> {
    let bytes = payload.get(index * 4..index * 4 + 4)?;
    Some(f32::from_le_bytes(bytes.try_into().ok()?))
}

fn decode_3(payload: &[u8], host_time_ms: f64) -> Option<TelemetrySample> {
    if payload.len() != PAYLOAD_3 { return None; }
    let yaw = read_f32(payload, 0)?;
    let pitch = read_f32(payload, 1)?;
    let roll = read_f32(payload, 2)?;
    if [yaw, pitch, roll].iter().any(|value| !value.is_finite())
        || yaw.abs() > 100_000.0 || pitch.abs() > 720.0 || roll.abs() > 720.0
    {
        return None;
    }
    Some(TelemetrySample {
        timestamp_ms: host_time_ms,
        roll,
        pitch,
        yaw,
        // The current USB stream carries Euler angles only. Build the
        // quaternion here so the 3D attitude view follows the device too.
        quaternion: euler_deg_to_quaternion(roll, pitch, yaw),
        gyro_dps: [0.0; 3],
        accel_g: [0.0; 3],
        vqf_us: 0.0,
        fusion_hz: 0.0,
        late: 0.0,
    })
}

fn euler_deg_to_quaternion(roll: f32, pitch: f32, yaw: f32) -> [f32; 4] {
    let (r, p, y) = (
        roll.to_radians() * 0.5,
        pitch.to_radians() * 0.5,
        yaw.to_radians() * 0.5,
    );
    let (sr, cr) = r.sin_cos();
    let (sp, cp) = p.sin_cos();
    let (sy, cy) = y.sin_cos();
    [
        cr * cp * cy + sr * sp * sy,
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
    ]
}

fn decode_16(payload: &[u8], host_time_ms: f64) -> Option<TelemetrySample> {
    if payload.len() != PAYLOAD_16 { return None; }
    let mut values = [0.0f32; CHANNELS_16];
    for (index, value) in values.iter_mut().enumerate() { *value = read_f32(payload, index)?; }
    if values.iter().any(|value| !value.is_finite())
        || values[0].abs() > 720.0 || values[1].abs() > 720.0 || values[2].abs() > 100_000.0
        || values[13] < 0.0 || values[13] > 1_000_000.0
        || values[14] < 0.0 || values[14] > 20_000.0 || values[15] < 0.0
    { return None; }
    Some(TelemetrySample {
        timestamp_ms: host_time_ms,
        roll: values[0], pitch: values[1], yaw: values[2],
        quaternion: [values[3], values[4], values[5], values[6]],
        gyro_dps: [values[7], values[8], values[9]],
        accel_g: [values[10], values[11], values[12]],
        vqf_us: values[13], fusion_hz: values[14], late: values[15],
    })
}

fn find_subslice(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|window| window == needle)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture3(yaw: f32, pitch: f32, roll: f32) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(16);
        for value in [yaw, pitch, roll] { bytes.extend_from_slice(&value.to_le_bytes()); }
        bytes.extend_from_slice(&TAIL);
        bytes
    }

    #[test]
    fn decodes_current_firmware_usb_cdc_3ch_frame() {
        let mut parser = JustFloatParser::new();
        let samples = parser.feed(&fixture3(42.0, -5.0, 10.0), 123.0);
        assert_eq!(samples.len(), 1);
        assert_eq!(samples[0].yaw, 42.0);
        assert_eq!(samples[0].pitch, -5.0);
        assert_eq!(samples[0].roll, 10.0);
        assert!(samples[0].quaternion[0] < 1.0);
        assert!(samples[0].quaternion.iter().all(|value| value.is_finite()));
        assert_eq!(parser.metrics().errors, 0);
    }

    #[test]
    fn handles_fragmented_usb_cdc_frame() {
        let bytes = fixture3(1.0, 2.0, 3.0);
        let mut parser = JustFloatParser::new();
        assert!(parser.feed(&bytes[..5], 1.0).is_empty());
        assert_eq!(parser.feed(&bytes[5..], 2.0).len(), 1);
    }

    #[test]
    fn decodes_multiple_usb_frames_in_one_read() {
        let mut bytes = fixture3(1.0, 2.0, 3.0);
        bytes.extend_from_slice(&fixture3(4.0, 5.0, 6.0));
        let mut parser = JustFloatParser::new();
        let samples = parser.feed(&bytes, 1.0);
        assert_eq!(samples.len(), 2);
        assert_eq!(samples[1].roll, 6.0);
    }
}
