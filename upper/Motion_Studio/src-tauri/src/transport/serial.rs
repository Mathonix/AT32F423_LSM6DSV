use std::{io::Read, time::Duration};

use super::{Result, Transport, TransportError};

pub struct SerialTransport {
    port_name: String,
    baud_rate: u32,
    port: Option<Box<dyn serialport::SerialPort>>,
}

impl SerialTransport {
    pub fn new(port_name: String, baud_rate: u32) -> Self {
        Self { port_name, baud_rate, port: None }
    }
}

impl Transport for SerialTransport {
    fn connect(&mut self) -> Result<()> {
        if self.port_name.trim().is_empty() {
            return Err(TransportError::InvalidConfig("select a serial port".into()));
        }
        let port = serialport::new(&self.port_name, self.baud_rate)
            .timeout(Duration::from_millis(8))
            .open()
            .map_err(|error| TransportError::Io(error.to_string()))?;
        self.port = Some(port);
        Ok(())
    }

    fn disconnect(&mut self) {
        self.port.take();
    }

    fn read(&mut self, buffer: &mut [u8]) -> Result<usize> {
        let port = self.port.as_mut().ok_or(TransportError::NotConnected)?;
        match port.read(buffer) {
            Ok(count) => Ok(count),
            Err(error) if error.kind() == std::io::ErrorKind::TimedOut => Ok(0),
            Err(error) => Err(TransportError::Io(error.to_string())),
        }
    }

    fn label(&self) -> String {
        format!("{} · {} baud", self.port_name, self.baud_rate)
    }
}
