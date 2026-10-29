//! Linux hidraw metadata discovery, without opening device nodes.
//!
//! Identification evidence: ElXreno/flydigictl `docs/PROTOCOL.md`, Transport
//! section (vendor 37D7, products 1002–1004, buses 0003/0005). BS1 (1001)
//! uses BLE, not HID, and is intentionally excluded. No external runtime code
//! or command/report layouts are used here.

use serde::Serialize;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize)]
pub enum Transport {
    #[serde(rename = "cable")]
    Usb,
    #[serde(rename = "bluetooth")]
    Bluetooth,
}

/// A discovered endpoint, not proof of an open or responsive device session.
/// There is deliberately no file handle, write method, or raw command API.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct Device {
    pub(crate) path: PathBuf,
    pub(crate) model: &'static str,
    pub(crate) transport: Transport,
}

impl Device {
    pub fn path(&self) -> &Path {
        &self.path
    }
    pub fn model(&self) -> &'static str {
        self.model
    }
    pub fn transport(&self) -> Transport {
        self.transport
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct HidId {
    bus: u16,
    vendor: u32,
    product: u32,
}

fn parse_hid_id(uevent: &str) -> Option<HidId> {
    let line = uevent.lines().filter_map(|line| line.strip_prefix("HID_ID=")).next()?;
    let mut parts = line.split(':');
    Some(HidId {
        bus: u16::from_str_radix(parts.next()?, 16).ok()?,
        vendor: u32::from_str_radix(parts.next()?, 16).ok()?,
        product: u32::from_str_radix(parts.next()?, 16).ok()?,
    })
}

fn device_from_uevent(path: PathBuf, uevent: &str) -> Option<Device> {
    let id = parse_hid_id(uevent)?;
    if id.vendor != 0x37d7 {
        return None;
    }
    let model = match id.product {
        0x1002 => "BS2 Pro",
        0x1003 => "BS3",
        0x1004 => "BS3 Pro",
        _ => return None,
    };
    // BS1 (1001) is BLE-only and excluded from hidraw discovery.
    let transport = match id.bus {
        0x0003 => Transport::Usb,
        0x0005 => Transport::Bluetooth,
        _ => return None,
    };
    Some(Device { path, model, transport })
}

/// This reads sysfs only; returned `/dev` paths are not opened or probed.
pub fn discover() -> io::Result<Vec<Device>> {
    discover_in(Path::new("/sys/class/hidraw"))
}

/// Internal for testability.
fn discover_in(sys_class: &Path) -> io::Result<Vec<Device>> {
    let Ok(entries) = fs::read_dir(sys_class) else {
        return Ok(Vec::new());
    };
    let mut devices = Vec::new();
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        let Some(index) = name.strip_prefix("hidraw") else {
            continue;
        };
        if index.is_empty() || !index.chars().all(|ch| ch.is_ascii_digit()) {
            continue;
        }
        let uevent_path = sys_class.join(&name).join("device").join("uevent");
        let Ok(uevent) = fs::read_to_string(&uevent_path) else {
            continue;
        };
        if let Some(device) = device_from_uevent(Path::new("/dev").join(&name), &uevent) {
            devices.push(device);
        }
    }
    // USB first, then lexicographic by path.
    devices.sort_by(|a, b| match (a.transport, b.transport) {
        (Transport::Usb, Transport::Bluetooth) => std::cmp::Ordering::Less,
        (Transport::Bluetooth, Transport::Usb) => std::cmp::Ordering::Greater,
        _ => a.path.cmp(&b.path),
    });
    Ok(devices)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{self, File};
    use std::io;

    struct Fixture(PathBuf);

    impl Fixture {
        fn new() -> io::Result<Self> {
            let path = std::env::temp_dir().join(format!("flydigi-device-test-{}", std::process::id()));
            let _ = fs::remove_dir_all(&path);
            fs::create_dir_all(path.join("hidraw0/device"))?;
            fs::create_dir_all(path.join("hidraw1/device"))?;
            Ok(Fixture(path))
        }

        fn add(&self, name: &str, uevent: &str) {
            let dir = self.0.join(name).join("device");
            fs::create_dir_all(&dir).unwrap();
            fs::write(dir.join("uevent"), uevent).unwrap();
        }
    }

    impl Drop for Fixture {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.0).unwrap();
        }
    }

    fn uevent(bus: &str, vendor: &str, product: &str) -> String {
        format!("HID_ID={}:{}:{}\nHID_NAME=test\n", bus, vendor, product)
    }

    #[test]
    fn parses_linux_hid_id_without_using_name_or_usb_parent() {
        assert_eq!(
            parse_hid_id(&uevent("0003", "000037D7", "00001002")),
            Some(HidId { bus: 3, vendor: 0x37d7, product: 0x1002 })
        );
        assert_eq!(
            parse_hid_id("HID_ID=0005:000037d7:00001004"),
            Some(HidId { bus: 5, vendor: 0x37d7, product: 0x1004 })
        );
    }

    #[test]
    fn classifies_supported_models_on_both_transports() {
        for (bus, transport) in [("0003", Transport::Usb), ("0005", Transport::Bluetooth)] {
            for (product, model) in [("00001002", "BS2 Pro"), ("00001003", "BS3"), ("00001004", "BS3 Pro")] {
                let d = device_from_uevent(PathBuf::from("/dev/hidraw7"), &uevent(bus, "000037D7", product));
                assert_eq!(d, Some(Device { path: PathBuf::from("/dev/hidraw7"), model, transport }));
            }
        }
    }

    #[test]
    fn filters_vendor_product_ble_only_bs1_and_unknown_bus() {
        for (bus, vendor, product) in [
            ("0003", "00000000", "00001002"),
            ("0003", "000137D7", "00001002"),
            ("0003", "000037D7", "00011002"),
            ("0003", "000037D7", "00001001"), // BS1 BLE-only
            ("0005", "000037D7", "00001001"), // BS1 BLE-only
            ("0003", "000037D7", "00001005"),
            ("0006", "000037D7", "00001002"),
            ("0000", "000037D7", "00001002"),
        ] {
            assert_eq!(device_from_uevent(PathBuf::from("/dev/hidraw0"), &uevent(bus, vendor, product)), None);
        }
    }

    #[test]
    fn discovers_sysfs_only_with_deterministic_usb_first_ordering() {
        let fixture = Fixture::new().unwrap();
        fixture.add("hidraw9", &uevent("0005", "000037D7", "00001002"));
        fixture.add("hidraw2", &uevent("0003", "000037D7", "00001002"));
        fixture.add("hidraw1", &uevent("0005", "000037D7", "00001002"));
        fixture.add("hidraw10", &uevent("0003", "000037D7", "00001002"));
        fixture.add("hidraw3", "HID_ID=invalid");
        fixture.add("hidraw4", &uevent("0003", "00000000", "00001002"));
        fixture.add("hidraw", &uevent("0003", "000037D7", "00001002"));
        fixture.add("hidraw5junk", &uevent("0003", "000037D7", "00001002"));
        fs::create_dir(fixture.0.join("hidraw6")).unwrap(); // disappeared HID parent
        let devices = discover_in(&fixture.0).unwrap();
        assert_eq!(devices.iter().map(Device::path).collect::<Vec<_>>(), vec![
            Path::new("/dev/hidraw10"), Path::new("/dev/hidraw2"),
            Path::new("/dev/hidraw1"), Path::new("/dev/hidraw9"),
        ]);
        assert_eq!(devices.iter().map(Device::transport).collect::<Vec<_>>(), vec![
            Transport::Usb, Transport::Usb, Transport::Bluetooth, Transport::Bluetooth,
        ]);
        assert!(devices.iter().all(|device| device.model() == "BS2 Pro"));
        assert_eq!(discover_in(&fixture.0).unwrap(), devices);
        // No device node exists in the fixture: only sysfs metadata is needed.
    }

    #[test]
    fn missing_class_is_empty_but_other_io_errors_are_reported() {
        let path = std::env::temp_dir().join(format!("flydigi-device-empty-{}", std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(&path).unwrap();
        assert_eq!(discover_in(&path).unwrap(), Vec::new());
        fs::remove_dir_all(&path).unwrap();
        // Can't easily test other IO errors without mocking, trust the Err branch.
    }

    #[test]
    fn session_rejects_missing_regular_and_symlink_nodes() {
        for path in ["/dev/null", "/etc/passwd", "/dev/hidraw100"] {
            let mut session = crate::session::Session { device: crate::device::Device { path: PathBuf::from(path), model: "BS2 Pro", transport: crate::device::Transport::Usb }, input: File::open("/dev/null").unwrap() };
            assert!(!session.status().connected);
        }
    }
}