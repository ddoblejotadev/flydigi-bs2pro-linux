//! Pure HID output command encoding.
//!
//! Encoders validate all host inputs before producing complete transport reports.
//! They do not open devices, write bytes, or update daemon state.

use crate::device::Transport;
use thiserror::Error;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Gear {
    Quiet,
    Standard,
    Strong,
    Overclock,
}

impl Gear {
    fn index(self) -> u8 {
        match self {
            Self::Quiet => 0,
            Self::Standard => 1,
            Self::Strong => 2,
            Self::Overclock => 3,
        }
    }
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum EncodeError {
    #[error("rpm must be between 500 and 4000")]
    RpmOutOfRange,
    #[error("rpm {rpm} exceeds supply limit {limit}")]
    SupplyLimit { rpm: u16, limit: u16 },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Report {
    pub bytes: Vec<u8>,
}

impl Report {
    pub fn command(&self, transport: Transport) -> u8 {
        self.bytes[match transport {
            Transport::Bluetooth => 3,
            Transport::Usb => 2,
        }]
    }
}

pub fn encode_set_automatic(transport: Transport) -> Vec<Report> {
    vec![encode(transport, 0x23, &[])]
}

/// Query the six-LED side strip power flag.
pub fn encode_get_rgb_power(transport: Transport) -> Report {
    encode(transport, 0x45, &[])
}

/// Toggle the six-LED side strip. Firmware command 0x46 accepts only 0/1.
pub fn encode_set_rgb_power(transport: Transport, enabled: bool) -> Vec<Report> {
    vec![encode(transport, 0x46, &[u8::from(enabled)])]
}

pub fn encode_set_manual(transport: Transport, rpm: u16, supply_limit: u16) -> Result<Vec<Report>, EncodeError> {
    if !(500..=4000).contains(&rpm) {
        return Err(EncodeError::RpmOutOfRange);
    }
    if rpm > supply_limit {
        return Err(EncodeError::SupplyLimit { rpm, limit: supply_limit });
    }
    Ok(vec![
        encode(transport, 0x23, &[]),
        encode(transport, 0x21, &[(rpm & 0xff) as u8, (rpm >> 8) as u8]),
    ])
}

/// Encode fixed-gear selection. The caller supplies fresh supply information;
/// no clamping is performed.
pub fn encode_set_gear(
    transport: Transport,
    gear: Gear,
    rpm: u16,
    supply_limit: u16,
) -> Result<Vec<Report>, EncodeError> {
    if !(500..=4000).contains(&rpm) {
        return Err(EncodeError::RpmOutOfRange);
    }
    if rpm > supply_limit {
        return Err(EncodeError::SupplyLimit { rpm, limit: supply_limit });
    }
    // 0x26 selects the gear and exits realtime on the BS2 Pro. The separate
    // 0x24 entry command is rejected by this firmware with ACK payload 0x02.
    Ok(vec![encode(
        transport,
        0x26,
        &[gear.index(), (rpm & 0xff) as u8, (rpm >> 8) as u8],
    )])
}

fn encode(transport: Transport, command: u8, payload: &[u8]) -> Report {
    let length = (payload.len() + 2) as u8;
    let mut frame = vec![0x5a, 0xa5, command, length];
    frame.extend_from_slice(payload);
    let checksum = frame[2..].iter().fold(0u8, |sum, byte| sum.wrapping_add(*byte));
    frame.push(checksum);

    let report_len = match transport {
        Transport::Bluetooth => 25,
        Transport::Usb => 31,
    };
    let mut bytes = if transport == Transport::Bluetooth {
        let mut prefixed = vec![0x02];
        prefixed.extend_from_slice(&frame);
        prefixed
    } else {
        frame
    };
    bytes.resize(report_len, 0);
    Report { bytes }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encodes_official_gear_sequence_for_bluetooth() {
        let reports = encode_set_gear(Transport::Bluetooth, Gear::Strong, 3300, 3300).unwrap();
        assert_eq!(reports.len(), 1);
        assert_eq!(&reports[0].bytes[..10], &[2, 0x5a, 0xa5, 0x26, 0x05, 0x02, 0xe4, 0x0c, 0x1d, 0x00]);
        assert_eq!(reports[0].bytes.len(), 25);
    }

    #[test]
    fn encodes_rgb_power_toggle_for_bluetooth() {
        let reports = encode_set_rgb_power(Transport::Bluetooth, true);
        assert_eq!(&reports[0].bytes[..7], &[2, 0x5a, 0xa5, 0x46, 0x03, 0x01, 0x4a]);
        let off = encode_set_rgb_power(Transport::Bluetooth, false);
        assert_eq!(&off[0].bytes[..7], &[2, 0x5a, 0xa5, 0x46, 0x03, 0x00, 0x49]);
    }

    #[test]
    fn rejects_invalid_and_supply_limited_rpm_without_reports() {
        assert_eq!(encode_set_gear(Transport::Usb, Gear::Quiet, 499, 4000), Err(EncodeError::RpmOutOfRange));
        assert_eq!(encode_set_gear(Transport::Usb, Gear::Quiet, 4001, 4000), Err(EncodeError::RpmOutOfRange));
        assert_eq!(encode_set_gear(Transport::Usb, Gear::Strong, 3300, 2700), Err(EncodeError::SupplyLimit { rpm: 3300, limit: 2700 }));
    }
}
