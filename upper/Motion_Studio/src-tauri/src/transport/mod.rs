mod mock;
mod replay;
mod serial;

pub use mock::MockTransport;
pub use replay::ReplayTransport;
pub use serial::SerialTransport;

use std::{fmt, time::Duration};

#[derive(Debug)]
pub enum TransportError {
    NotConnected,
    Io(String),
    InvalidConfig(String),
}

impl fmt::Display for TransportError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NotConnected => write!(f, "transport is not connected"),
            Self::Io(message) => write!(f, "transport I/O error: {message}"),
            Self::InvalidConfig(message) => write!(f, "invalid transport configuration: {message}"),
        }
    }
}

impl std::error::Error for TransportError {}

pub type Result<T> = std::result::Result<T, TransportError>;

pub trait Transport: Send {
    fn connect(&mut self) -> Result<()>;
    fn disconnect(&mut self);
    fn read(&mut self, buffer: &mut [u8]) -> Result<usize>;
    fn label(&self) -> String;
    fn idle_delay(&self) -> Duration { Duration::from_millis(1) }
}
