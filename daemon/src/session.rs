//! Read-only, bounded status snapshots with wake sequence.
//!
//! The device requires a `SetStandby(Off)` command followed by a brief pause
//! before it begins emitting spontaneous status reports. The `wake()` method
//! on `Session` performs this sequence and can be called before `status()`.
//! After wake, the first `status` read will return `connected=true` with
//! valid RPM/gear/supply data.

use crate::command::Report;
use crate::device::{self, Device, Transport};
use crate::protocol::{self, Gear, Mode, Supply};
use serde::Serialize;
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::unix::fs::{FileTypeExt, OpenOptionsExt};
use std::thread;
use std::time::{Duration, Instant};

/// Maximum wait for an unsolicited status report on the preferred endpoint.
pub const STATUS_TIMEOUT: Duration = Duration::from_millis(250);

#[derive(Debug, Serialize, PartialEq)]
pub struct StatusResponse {
    pub reply: &'static str,
    pub model: &'static str,
    pub connected: bool,
    pub current_rpm: Option<u16>,
    pub target_rpm: Option<u16>,
    pub smart_mode: Option<bool>,
    pub fixed_gear: bool,
    pub manual: Option<bool>,
    pub manual_rpm: Option<u16>,
    pub supply: Option<&'static str>,
    pub supply_max_rpm: Option<u16>,
    pub transport: Option<Transport>,
    pub lighting: Option<serde_json::Value>,
    pub strip_on: Option<bool>,
    pub revision: u64,
    pub demands: Vec<serde_json::Value>,
}

impl StatusResponse {
    pub fn disconnected() -> Self {
        Self {
            reply: "status",
            model: "",
            connected: false,
            current_rpm: None,
            target_rpm: None,
            smart_mode: Some(false),
            fixed_gear: false,
            manual: Some(false),
            manual_rpm: None,
            supply: None,
            supply_max_rpm: None,
            transport: None,
            lighting: None,
            strip_on: None,
            revision: 0,
            demands: Vec::new(),
        }
    }

    /// Decode a complete report with metadata supplied by discovery.
    pub fn from_report(
        report: &[u8],
        model: &'static str,
        transport: Transport,
    ) -> Result<Self, protocol::ParseError> {
        let status = protocol::parse_status(report, model, transport)?;
        Ok(Self {
            reply: "status",
            model,
            connected: true,
            current_rpm: Some(status.current_rpm),
            target_rpm: Some(status.target_rpm),
            smart_mode: Some(status.mode == Mode::Realtime),
            fixed_gear: status.mode == Mode::Gear,
            manual: Some(false),
            manual_rpm: None,
            supply: match status.supply {
                Supply::Undecided => None,
                Supply::Low => Some("low"),
                Supply::Medium => Some("medium"),
                Supply::Full => Some("full"),
            },
            supply_max_rpm: Some(status.supply.max_rpm()),
            transport: Some(transport),
            lighting: None,
            strip_on: None,
            revision: 0,
            demands: Vec::new(),
        })
    }
}

/// Owns only a read-only handle obtained from a discovered Device, never an IPC path.
pub struct Session {
    pub(crate) device: Device,
    pub(crate) input: File,
}

impl Session {
    pub fn open(device: Device) -> io::Result<Self> {
        let input = open_endpoint(&device, false)?;
        Ok(Self { device, input })
    }

    pub fn open_writable(device: Device) -> io::Result<Self> {
        let input = open_endpoint(&device, true)?;
        Ok(Self { device, input })
    }

    /// Wake the device by sending SetStandby(Off) if it appears asleep.
    /// Requires a writable handle; caller must open with writable=true.
    pub fn wake(&mut self) -> io::Result<()> {
        // Send SetStandby(Off) = 0x0D with value 0
        let report = match self.device.transport() {
            Transport::Bluetooth => vec![0x02, 0x5a, 0xa5, 0x0d, 0x03, 0x00, 0x10],
            Transport::Usb => vec![0x5a, 0xa5, 0x0d, 0x03, 0x00, 0x10],
        };
        // Pad to report length
        let mut padded = report;
        padded.resize(
            if self.device.transport() == Transport::Bluetooth { 25 } else { 31 },
            0,
        );
        self.input.write_all(&padded)?;
        self.input.flush()?;
        // Give firmware time to process
        std::thread::sleep(Duration::from_millis(50));
        Ok(())
    }

    pub fn status(&mut self) -> StatusResponse {
        read_status(&mut self.input, self.device.model(), self.device.transport(), STATUS_TIMEOUT)
    }

    pub fn query_rgb_power(&mut self) -> Result<bool, String> {
        let report = crate::command::encode_get_rgb_power(self.device.transport());
        self.input.write_all(&report.bytes).map_err(|error| error.to_string())?;
        self.input.flush().map_err(|error| error.to_string())?;
        let started = Instant::now();
        let mut raw = [0u8; 33];
        while started.elapsed() < STATUS_TIMEOUT {
            match self.input.read(&mut raw) {
                Ok(length) => {
                    if let Ok(frame) = protocol::parse_frame(&raw[..length], self.device.transport()) {
                        if frame.command() == 0x45 {
                            return frame.payload().first().copied().map(|value| value != 0)
                                .ok_or_else(|| "RGB power readback payload was empty".to_string());
                        }
                    }
                }
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => thread::sleep(Duration::from_millis(2)),
                Err(error) => return Err(error.to_string()),
            }
        }
        Err("timed out waiting for RGB power readback".to_string())
    }

    /// Send reports and require a positive ACK for each command.
    pub fn execute_reports(&mut self, reports: &[Report]) -> io::Result<()> {
        for report in reports {
            self.input.write_all(&report.bytes)?;
            self.input.flush()?;
            thread::sleep(Duration::from_millis(5));
            wait_for_ack(&mut self.input, self.device.transport(), report.command(self.device.transport()))?;
        }
        Ok(())
    }
}

/// Shared guarded open for snapshots and command execution. Never accepts an
/// IPC-supplied path, creates/truncates a file, or follows a final symlink.
pub(crate) fn open_endpoint(device: &Device, writable: bool) -> io::Result<File> {
    // Linux flags; std also applies CLOEXEC. NOCTTY prevents acquiring a
    // controlling terminal if a node is replaced during discovery.
    const O_NONBLOCK: i32 = 0x800;
    const O_NOFOLLOW: i32 = 0x20000;
    const O_NOCTTY: i32 = 0x100;
    let input = OpenOptions::new().read(true).write(writable)
        .custom_flags(O_NONBLOCK | O_NOFOLLOW | O_NOCTTY).open(device.path())?;
    if !input.metadata()?.file_type().is_char_device() {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "not a hidraw character device"));
    }
    // Reuse discovery's identity validation for both access modes.
    if !device::discover()?.contains(device) {
        return Err(io::Error::new(io::ErrorKind::NotFound, "device disappeared"));
    }
    Ok(input)
}

fn ack_is_positive(command: u8, payload: &[u8]) -> bool {
    payload == [1] || (command == 0x23 && payload == [3])
}

fn wait_for_ack(input: &mut impl Read, transport: Transport, command: u8) -> io::Result<()> {
    let started = Instant::now();
    let mut report = [0u8; 33];
    while started.elapsed() < STATUS_TIMEOUT {
        match input.read(&mut report) {
            Ok(length) => {
                if let Ok(frame) = protocol::parse_frame(&report[..length], transport) {
                    if frame.command() == command {
                        if ack_is_positive(command, frame.payload()) {
                            return Ok(());
                        }
                        let payload = frame.payload().iter().map(|byte| format!("{byte:02x}")).collect::<Vec<_>>().join(" ");
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidData,
                            format!("negative HID ACK command 0x{command:02x}, payload [{payload}]"),
                        ));
                    }
                }
            }
            Err(error) if matches!(error.kind(), io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted) => {}
            Err(error) => return Err(error),
        }
        thread::sleep(Duration::from_millis(5).min(STATUS_TIMEOUT.saturating_sub(started.elapsed())));
    }
    Err(io::Error::new(io::ErrorKind::TimedOut, "missing HID ACK"))
}

fn read_status(
    input: &mut impl Read, model: &'static str, transport: Transport, timeout: Duration,
) -> StatusResponse {
    let started = Instant::now();
    // One extra byte prevents an oversized report from being accepted as a
    // valid truncated report. Reads are individual HID reports, not a stream
    // to concatenate into a valid frame.
    let mut report = [0u8; 33];
    while started.elapsed() < timeout {
        match input.read(&mut report) {
            Ok(0) => break,
            Ok(length) => {
                if let Ok(status) = StatusResponse::from_report(&report[..length], model, transport) {
                    return status;
                }
            }
            Err(error) if matches!(error.kind(), io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted) => {}
            Err(_) => break,
        }
        // Bound even malformed-report floods and interruptions by the same
        // deadline, without spinning or extending it on each incoming frame.
        thread::sleep(Duration::from_millis(5).min(timeout.saturating_sub(started.elapsed())));
    }
    StatusResponse::disconnected()
}

/// Rediscover for each snapshot; never retain a previous connected response.
/// Only the first USB-first endpoint is tried; no fallback opens on failure.
/// This read-only access independently of how many devices are present.
fn snapshot_from_discovery(devices: io::Result<Vec<Device>>) -> StatusResponse {
    let devices = match devices {
        Ok(devices) if !devices.is_empty() => devices,
        _ => return StatusResponse::disconnected(),
    };
    let device = &devices[0];
    let mut session = match Session::open(device.clone()) {
        Ok(s) => s,
        Err(_) => return StatusResponse::disconnected(),
    };
    session.status()
}

/// Wake the device and read a status snapshot.
/// Discovers, opens writable, sends SetStandby(Off), waits 50ms, then reads status.
/// Returns disconnected status on any discovery/open/wake/read failure.
fn prepare_command_session() -> Result<(Session, StatusResponse), String> {
    let devices = device::discover().map_err(|error| error.to_string())?;
    let device = devices.first().cloned().ok_or_else(|| "no device yet".to_string())?;
    let mut session = Session::open_writable(device).map_err(|error| error.to_string())?;
    session.wake().map_err(|error| error.to_string())?;
    let before = session.status();
    if !before.connected {
        return Err("device did not return status after wake".to_string());
    }
    Ok((session, before))
}

fn finish_command(session: &mut Session, reports: &[Report]) -> Result<StatusResponse, String> {
    session.execute_reports(reports).map_err(|error| error.to_string())?;
    let after = session.status();
    if !after.connected {
        return Err("command not confirmed by status readback".to_string());
    }
    Ok(after)
}

pub fn execute_set_gear(gear: crate::command::Gear, rpm: u16) -> Result<StatusResponse, String> {
    let (mut session, before) = prepare_command_session()?;
    let limit = before.supply_max_rpm.ok_or_else(|| "supply is undecided".to_string())?;
    let reports = crate::command::encode_set_gear(session.device.transport(), gear, rpm, limit)
        .map_err(|error| error.to_string())?;
    finish_command(&mut session, &reports)
}

pub fn execute_set_rgb_power(enabled: bool) -> Result<StatusResponse, String> {
    let (mut session, _) = prepare_command_session()?;
    let reports = crate::command::encode_set_rgb_power(session.device.transport(), enabled);
    let status = finish_command(&mut session, &reports)?;
    let strip_on = session.query_rgb_power()?;
    if strip_on != enabled {
        return Err("RGB command ACKed but strip power readback mismatched".to_string());
    }
    let mut confirmed = status;
    confirmed.strip_on = Some(strip_on);
    Ok(confirmed)
}

pub fn execute_set_automatic() -> Result<StatusResponse, String> {
    let (mut session, _) = prepare_command_session()?;
    let reports = crate::command::encode_set_automatic(session.device.transport());
    finish_command(&mut session, &reports)
}

pub fn execute_set_manual(rpm: u16) -> Result<StatusResponse, String> {
    let (mut session, before) = prepare_command_session()?;
    let limit = before.supply_max_rpm.ok_or_else(|| "supply is undecided".to_string())?;
    let reports = crate::command::encode_set_manual(session.device.transport(), rpm, limit)
        .map_err(|error| error.to_string())?;
    finish_command(&mut session, &reports)
}

pub fn wake_and_status() -> StatusResponse {
    let devices = match device::discover() {
        Ok(devices) if !devices.is_empty() => devices,
        _ => return StatusResponse::disconnected(),
    };
    let device = &devices[0];
    let mut session = match Session::open_writable(device.clone()) {
        Ok(s) => s,
        Err(_) => return StatusResponse::disconnected(),
    };
    if let Err(_) = session.wake() {
        return StatusResponse::disconnected();
    }
    session.status()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device::Transport;
    use crate::protocol::{Gear, Mode, Standby, Supply};
    use serde_json::json;
    use std::collections::VecDeque;
    use std::io;
    use std::path::PathBuf;
    use std::time::{Duration, Instant};

    const STATUS_TIMEOUT_TEST: Duration = Duration::from_millis(20);

    fn frame(transport: Transport, command: u8, payload: &[u8]) -> Vec<u8> {
        let mut bytes = match transport {
            Transport::Bluetooth => vec![2],
            Transport::Usb => vec![],
        };
        let length = (payload.len() + 2) as u8;
        bytes.extend_from_slice(&[0x5a, 0xa5, command, length]);
        bytes.extend_from_slice(payload);
        let checksum_start = if transport == Transport::Bluetooth { 3 } else { 2 };
        bytes.push(bytes[checksum_start..].iter().fold(0u8, |s, b| s.wrapping_add(*b)));
        bytes.resize(if transport == Transport::Usb { 32 } else { 25 }, 0);
        bytes
    }

    fn status(transport: Transport, supply: u8, mode: u8, gear: u8, rpm: u16) -> Vec<u8> {
        let [lo, hi] = rpm.to_le_bytes();
        frame(transport, 0xef, &[supply << 5 | gear << 1, mode, 0, lo, hi, lo, hi, 0, 0, 0, 0])
    }

    fn report(transport: Transport, supply: u8, mode: u8, gear: u8, rpm: u16) -> Vec<u8> {
        let [lo, hi] = rpm.to_le_bytes();
        frame(transport, 0xef, &[supply << 5 | gear << 1, mode, 0, lo, hi, lo, hi, 0, 0, 0, 0])
    }

    struct Reports(VecDeque<io::Result<Vec<u8>>>);

    impl Read for Reports {
        fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
            let bytes = self.0.pop_front().unwrap_or_else(|| Err(io::ErrorKind::WouldBlock.into()))?;
            out[..bytes.len()].copy_from_slice(&bytes);
            Ok(bytes.len())
        }
    }

    fn idle() -> io::Result<Vec<u8>> { Err(io::ErrorKind::WouldBlock.into()) }

    fn disconnected() -> StatusResponse {
        StatusResponse {
            reply: "status",
            model: "",
            connected: false,
            current_rpm: None,
            target_rpm: None,
            smart_mode: Some(false),
            fixed_gear: false,
            manual: Some(false),
            manual_rpm: None,
            supply: None,
            supply_max_rpm: None,
            transport: None,
            lighting: None,
            strip_on: None,
            revision: 0,
            demands: Vec::new(),
        }
    }

    #[test]
    fn accepts_realtime_mode_ack_and_rejects_other_non_success_payloads() {
        assert!(ack_is_positive(0x23, &[1]));
        assert!(ack_is_positive(0x23, &[3]));
        assert!(!ack_is_positive(0x21, &[3]));
        assert!(!ack_is_positive(0x23, &[2]));
    }

    #[test]
    fn synthetic_reports_map_exact_json_without_inventing_policy() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            for supply in [0, 1, 2, 3] {
                for mode in [0, 1] {
                    let mut reports = vec![
                        Ok(status(transport, supply, mode, 1, 1700)),
                        Ok(status(transport, supply, mode, 1, 1700)),
                    ];
                    let mut input = Reports(VecDeque::from(reports.drain(..).collect::<Vec<_>>()));
                    let status = read_status(&mut input, "BS2 Pro", transport, Duration::from_millis(20));
                    let wire = match supply {
                        0 => None,
                        1 => Some("low"),
                        2 => Some("medium"),
                        3 => Some("full"),
                        _ => None,
                    };
                    assert_eq!(status.connected, true);
                    assert_eq!(status.model, "BS2 Pro");
                    assert_eq!(status.transport, Some(transport));
                    assert_eq!(status.supply, wire);
                    assert_eq!(status.smart_mode, Some(mode == 1));
                    assert_eq!(status.fixed_gear, mode == 0);
                    assert_eq!(status.manual, Some(false));
                    assert_eq!(status.current_rpm, Some(1700));
                    assert_eq!(status.target_rpm, Some(1700));
                }
            }
        }
    }

    #[test]
    fn skips_invalid_reports_but_never_reuses_a_connected_snapshot() {
        let bytes = report(Transport::Bluetooth, 3, 1, 1, 1700);
        let mut corrupt = bytes.clone();
        corrupt[16] ^= 1;
        assert!(StatusResponse::from_report(&corrupt, "BS3 Pro", Transport::Bluetooth).is_err());
        let mut input = Reports(VecDeque::from([
            Ok(vec![0; 7]), Ok(corrupt), Ok(bytes.clone()), Ok(vec![]),
        ]));
        let timeout = Duration::from_millis(20);
        assert_eq!(serde_json::to_value(read_status(&mut input, "BS3 Pro", Transport::Bluetooth, timeout)).unwrap(),
            serde_json::to_value(StatusResponse::from_report(&bytes, "BS3 Pro", Transport::Bluetooth).unwrap()).unwrap());
        assert_eq!(serde_json::to_value(read_status(&mut input, "BS3 Pro", Transport::Bluetooth, timeout)).unwrap(), serde_json::to_value(disconnected()).unwrap());
    }

    #[test]
    fn read_errors_eof_and_silence_fail_closed_with_bounded_wait() {
        for error in [io::ErrorKind::PermissionDenied, io::ErrorKind::NotFound] {
            let mut input = Reports(VecDeque::from([Err(error.into())]));
            assert_eq!(read_status(&mut input, "BS3 Pro", Transport::Usb, Duration::from_millis(20)), disconnected());
        }
        let timeout = Duration::from_millis(15);
        let started = Instant::now();
        let mut input = Reports(VecDeque::from([idle()]));
        assert_eq!(read_status(&mut input, "BS3 Pro", Transport::Usb, Duration::from_millis(15)), disconnected());
        assert!(started.elapsed() >= Duration::from_millis(15));
        assert!(started.elapsed() < Duration::from_secs(1));
    }

    #[test]
    fn nonblocking_input_and_invalid_report_flood_share_a_fixed_deadline() {
        let bytes = report(Transport::Bluetooth, 3, 1, 1, 1700);
        let mut input = Reports(VecDeque::from([
            Ok(vec![0; 7]), Ok(bytes.clone()), Ok(vec![0; 7]), Ok(bytes),
        ]));
        let timeout = Duration::from_millis(20);
        let started = Instant::now();
        assert_eq!(read_status(&mut input, "BS3 Pro", Transport::Bluetooth, Duration::from_millis(20)).connected, true);
        assert!(started.elapsed() < Duration::from_secs(1));
    }

    #[test]
    fn no_device_or_discovery_failure_preserves_disconnected_response() {
        assert_eq!(snapshot_from_discovery(Ok(Vec::new())), disconnected());
        assert_eq!(snapshot_from_discovery(Err(io::ErrorKind::NotFound.into())), disconnected());
    }

    #[test]
    fn session_rejects_missing_regular_and_symlink_nodes() {
        for path in ["/dev/null", "/etc/passwd", "/dev/hidraw100"] {
            let mut session = Session { device: Device { path: PathBuf::from(path), model: "BS2 Pro", transport: Transport::Usb }, input: File::open("/dev/null").unwrap() };
            assert!(!session.status().connected);
        }
    }
}