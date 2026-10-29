//! Read-only HID input decoding. No device handles or command encoder live here.
//!
//! Evidence: ElXreno/flydigictl `docs/PROTOCOL.md` (Transport/Frames and the
//! captured status report), corrected by `docs/FIRMWARE.md` sections 1 and 5.
//! These are protocol descriptions, not imported implementation code. The
//! detailed status evidence is BS3 Pro firmware; other models need live checks.
//!
//! Model is not carried in status: callers supply `Device::model()` and
//! `Device::transport()` from discovery. Realtime does not distinguish host
//! manual control from host Smart Speed. Target RPM is commanded, not clamped.

use crate::device::Transport;
use thiserror::Error;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Error)]
pub enum ParseError {
    #[error("expected {expected} input report bytes, received {actual}")]
    ReportLength { expected: usize, actual: usize },
    #[error("invalid HID frame magic")]
    BadMagic,
    #[error("invalid frame length {0}")]
    FrameLength(u8),
    #[error("checksum mismatch: expected {expected:#04x}, received {actual:#04x}")]
    Checksum { expected: u8, actual: u8 },
    #[error("expected status command 0xef, received {0:#04x}")]
    NotStatus(u8),
    #[error("expected 9 or 11 status payload bytes, received {0}")]
    StatusLength(usize),
}

/// A validated envelope. Unknown command bytes remain opaque here.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Frame<'a> {
    command: u8,
    payload: &'a [u8],
}

impl<'a> Frame<'a> {
    pub fn command(&self) -> u8 { self.command }
    pub fn payload(&self) -> &'a [u8] { self.payload }
}

/// Parse one complete input report (Bluetooth: 25 bytes, USB: 32 bytes).
/// Magic is at offset 1 on both transports; byte 0 is not a portable report ID.
/// Padding is ignored and never included in the checksum or payload. The
/// 15-byte HID *output* payload limit is not imposed on incoming reports.
pub fn parse_frame(report: &[u8], transport: Transport) -> Result<Frame<'_>, ParseError> {
    let expected_len = match transport {
        Transport::Bluetooth => 25,
        Transport::Usb => 32,
    };
    if report.len() != expected_len {
        return Err(ParseError::ReportLength { expected: expected_len, actual: report.len() });
    }
    let start = if transport == Transport::Bluetooth { 1 } else { 0 };
    if report[start..start + 2] != [0x5a, 0xa5] {
        return Err(ParseError::BadMagic);
    }
    let command_at = start + 2;
    let length_at = start + 3;
    let length = report[length_at];
    let checksum_at = command_at + usize::from(length);
    if length < 2 || checksum_at >= report.len() {
        return Err(ParseError::FrameLength(length));
    }
    let expected = report[command_at..checksum_at]
        .iter()
        .fold(0u8, |s, b| s.wrapping_add(*b));
    if report[checksum_at] != expected {
        return Err(ParseError::Checksum { expected, actual: report[checksum_at] });
    }
    let payload_start = length_at + 1;
    Ok(Frame { command: report[command_at], payload: &report[payload_start..checksum_at] })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Mode {
    Gear,
    Realtime,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Standby {
    Off,
    Instant,
    Delayed,
    Unknown(u8),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Gear {
    Quiet,
    Standard,
    Strong,
    Overclock,
    Unknown,
}

impl Gear {
    pub fn index(self) -> Option<u8> {
        match self {
            Gear::Quiet => Some(0),
            Gear::Standard => Some(1),
            Gear::Strong => Some(2),
            Gear::Overclock => Some(3),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Supply {
    Undecided,
    Low,
    Medium,
    Full,
}

impl Supply {
    /// Return the maximum RPM for this supply level.
    /// Low = 2700, Medium = 3300, Full = 4000, Undecided = 4000 (safe default).
    pub fn max_rpm(self) -> u16 {
        match self {
            Supply::Low => 2700,
            Supply::Medium => 3300,
            Supply::Full | Supply::Undecided => 4000,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Status {
    /// Discovery metadata, never inferred from a status byte.
    pub model: &'static str,
    /// Current RPM reported by the tachometer.
    pub current_rpm: u16,
    /// Raw commanded RPM, not proof of the speed achievable at this supply.
    pub target_rpm: u16,
    pub mode: Mode,
    pub standby: Standby,
    /// Effective (supply-clamped) gear, not the stored selected gear.
    pub gear: Gear,
    pub supply: Supply,
    pub asleep: bool,
    /// Retained so unknown/reserved bits are not lost or mistaken for an enum.
    pub mode_bits: u8,
}

/// Decode the documented 11-byte 0xEF payload after validating its envelope.
/// Unknown standby values survive as `Unknown`; mode uses bit 0 independently
/// of standby, auto-start (bit 1), and reserved bits. All gear/supply bit
/// patterns are defined, including supply 0 (undecided). No state is updated.
pub fn parse_status(report: &[u8], model: &'static str, transport: Transport) -> Result<Status, ParseError> {
    let frame = parse_frame(report, transport)?;
    if frame.command() != 0xef {
        return Err(ParseError::NotStatus(frame.command()));
    }
    // BS2 Pro reports carry 9 status payload bytes; BS3 Pro captures carry
    // 11. Both formats expose the fields decoded below.
    if !matches!(frame.payload().len(), 9 | 11) {
        return Err(ParseError::StatusLength(frame.payload().len()));
    }
    let payload = frame.payload();
    let flags = payload[0];
    let mode_bits = payload[1];
    let gear = match (mode_bits >> 1) & 3 {
        0 => Gear::Quiet,
        1 => Gear::Standard,
        2 => Gear::Strong,
        3 => Gear::Overclock,
        _ => Gear::Unknown,
    };
    let standby = match (mode_bits >> 2) & 3 {
        0 => Standby::Off,
        1 => Standby::Instant,
        2 => Standby::Delayed,
        v => Standby::Unknown(v),
    };
    let supply = match flags >> 5 {
        0 => Supply::Undecided,
        1 => Supply::Low,
        2 => Supply::Medium,
        3 => Supply::Full,
        _ => Supply::Undecided,
    };
    let asleep = flags & 1 != 0;
    let current_rpm = u16::from_le_bytes([payload[3], payload[4]]);
    let target_rpm = u16::from_le_bytes([payload[5], payload[6]]);
    let mode = if mode_bits & 1 == 0 { Mode::Gear } else { Mode::Realtime };
    Ok(Status {
        model,
        current_rpm,
        target_rpm,
        mode,
        standby,
        gear,
        supply,
        asleep,
        mode_bits,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    // Documented hardware capture, independent of the synthetic builders below.
    const CAPTURE: [u8; 25] = [
        0x01, 0x5a, 0xa5, 0xef, 0x0d, 0x68, 0x03, 0x05,
        0x6c, 0x07, 0x6c, 0x07, 0x01, 0x01, 0xb4, 0x23,
        0x2b, 0, 0, 0, 0, 0, 0, 0, 0,
    ];

    fn report(command: u8, payload: &[u8], transport: Transport) -> Vec<u8> {
        let mut bytes = match transport {
            Transport::Bluetooth => vec![2],
            Transport::Usb => vec![],
        };
        let length = (payload.len() + 2) as u8;
        bytes.extend_from_slice(&[0x5a, 0xa5, command, length]);
        bytes.extend_from_slice(payload);
        let checksum_start = if transport == Transport::Bluetooth { 3 } else { 2 };
        bytes.push(bytes[checksum_start..].iter().fold(0u8, |s, b| s.wrapping_add(*b)));
        bytes.resize(if transport == Transport::Bluetooth { 25 } else { 32 }, 0);
        bytes
    }

    #[test]
    fn captured_status_decodes_without_inventing_model_or_host_policy() {
        let expected = Status {
            model: "BS3 Pro", current_rpm: 1900, target_rpm: 1900,
            mode: Mode::Realtime, standby: Standby::Off, gear: Gear::Standard,
            supply: Supply::Full, asleep: false, mode_bits: 3,
        };
        assert_eq!(parse_status(&CAPTURE, "BS3 Pro", Transport::Bluetooth), Ok(expected));
        let frame = parse_frame(&CAPTURE, Transport::Bluetooth).unwrap();
        assert_eq!(frame.command(), 0xef);
    }

    #[test]
    fn bs2_pro_nine_byte_status_payload_decodes() {
        let payload = [0x68, 0x03, 0x05, 0x14, 0x05, 0x14, 0x05, 0xc2, 0x00];
        let bytes = report(0xef, &payload, Transport::Bluetooth);
        let status = parse_status(&bytes, "BS2 Pro", Transport::Bluetooth).unwrap();
        assert_eq!(status.current_rpm, 1300);
        assert_eq!(status.target_rpm, 1300);
        assert_eq!(status.supply, Supply::Full);
        assert!(!status.asleep);
    }

    #[test]
    fn envelope_accepts_empty_and_capacity_payloads_and_unknown_commands() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            let bytes = report(0x7e, &[], transport);
            let frame = parse_frame(&bytes, transport).unwrap();
            assert_eq!(frame.command(), 0x7e);
            assert_eq!(parse_status(&bytes, "BS2 Pro", transport), Err(ParseError::NotStatus(0x7e)));
        }
    }

    #[test]
    fn prefix_and_padding_are_not_part_of_frame_or_checksum() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            let payload = &CAPTURE[5..16];
            let mut bytes = report(0xef, payload, transport);
            // Bluetooth report ID is outside the frame; USB starts at magic.
            if transport == Transport::Bluetooth {
                bytes[0] ^= 1;
            }
            assert_eq!(parse_frame(&bytes, transport).unwrap().command(), 0xef);
            // Padding zeros after checksum are ignored.
            let capacity = if transport == Transport::Usb { 26 } else { 19 };
            for len in [0, 1, 15, capacity] {
                let payload = vec![0xff; len];
                let bytes = report(0x7e, &payload, transport);
                let frame = parse_frame(&bytes, transport).unwrap();
                assert_eq!(frame.command(), 0x7e);
                assert_eq!(parse_status(&bytes, "BS2 Pro", transport), Err(ParseError::NotStatus(0x7e)));
            }
        }
    }

    #[test]
    fn rejects_bad_magic_and_checksum_without_changing_input() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            let mut bytes = report(0xef, &CAPTURE[5..16], transport);
            let magic = if transport == Transport::Bluetooth { 1 } else { 0 };
            bytes[magic] ^= 1;
            assert_eq!(parse_frame(&bytes, transport), Err(ParseError::BadMagic));
            bytes[magic] ^= 1;
            let checksum = if transport == Transport::Bluetooth { 16 } else { 15 };
            bytes[checksum] ^= 1;
            assert!(matches!(parse_frame(&bytes, transport), Err(ParseError::Checksum { .. })));
            assert!(matches!(parse_status(&bytes, "BS3 Pro", transport), Err(ParseError::Checksum { .. })));
        }
    }

    #[test]
    fn rejects_underflow_and_out_of_report_declared_lengths() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            let mut bytes = report(0xef, &CAPTURE[5..16], transport);
            for len in [0, 1, (bytes.len() - 3) as u8, 255] {
                let mut bytes = bytes.clone();
                let length_at = if transport == Transport::Bluetooth { 4 } else { 3 };
                bytes[length_at] = len;
                assert!(parse_frame(&bytes, transport).is_err());
                assert!(parse_status(&bytes, "BS3 Pro", transport).is_err());
            }
        }
    }

    #[test]
    fn status_requires_exact_payload_size_even_with_valid_checksum() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            for len in [0, 1, 10, 12, 19] {
                let bytes = report(0xef, &vec![0; len], transport);
                assert!(parse_frame(&bytes, transport).is_ok());
                assert_eq!(parse_status(&bytes, "BS3 Pro", transport), Err(ParseError::StatusLength(len)));
            }
        }
    }

    #[test]
    fn gear_supply_and_sleep_ignore_link_and_demo_flags() {
        // bit 2 of flags is link, bit 3 is demo; neither affects supply/sleep/gear
        for transport in [Transport::Usb, Transport::Bluetooth] {
            for link_demo in [0, 4, 8, 12] {
                let mut payload = CAPTURE[5..16].to_vec();
                payload[0] = (payload[0] & 0xF3) | link_demo; // keep supply/asleep, set link/demo
                let bytes = report(0xef, &payload, transport);
                let status = parse_status(&bytes, "BS3 Pro", transport).unwrap();
                assert_eq!(status.supply, Supply::Full);
                assert!(!status.asleep);
            }
        }
    }

    #[test]
    fn mode_standby_and_unknown_bits_are_independent() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            for mode in [0, 1] {
                for standby in [0, 1, 2, 3] {
                    let mut payload = CAPTURE[5..16].to_vec();
                    payload[1] = (mode & 1) | ((standby & 3) << 2);
                    let bytes = report(0xef, &payload, transport);
                    let status = parse_status(&bytes, "BS3 Pro", transport).unwrap();
                    assert_eq!(status.mode, if mode == 0 { Mode::Gear } else { Mode::Realtime });
                    assert_eq!(status.standby, match standby {
                        0 => Standby::Off,
                        1 => Standby::Instant,
                        2 => Standby::Delayed,
                        3 => Standby::Unknown(3),
                        _ => unreachable!(),
                    });
                }
            }
        }
    }

    #[test]
    fn rpm_is_little_endian_unclamped_and_not_taken_from_other_settings() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            for (current, target) in [(0u16, 65535u16), (0x1234, 0xabcd)] {
                let mut payload = [0xff; 11];
                payload[3..5].copy_from_slice(&current.to_le_bytes());
                payload[5..7].copy_from_slice(&target.to_le_bytes());
                let bytes = report(0xef, &payload, transport);
                let status = parse_status(&bytes, "BS3 Pro", transport).unwrap();
                assert_eq!(status.current_rpm, current);
                assert_eq!(status.target_rpm, target);
            }
        }
    }

    #[test]
    fn truncated_and_oversized_reports_are_rejected_on_each_transport() {
        for transport in [Transport::Usb, Transport::Bluetooth] {
            for len in [0, 1, 15, if transport == Transport::Usb { 33 } else { 26 }] {
                let payload = vec![0xff; len];
                let mut bytes = report(0x7e, &payload, transport);
                bytes.truncate(len);
                assert!(parse_frame(&bytes, transport).is_err());
            }
        }
    }

}