//! Read-only Linux hwmon temperature discovery.

use serde::Serialize;
use std::fs;
use std::path::Path;

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct SensorReading {
    pub kind: &'static str,
    pub hwmon: String,
    pub device: String,
    pub kernel: String,
    pub label: String,
    pub temp_c: i32,
}

fn read_trimmed(path: &Path) -> Option<String> {
    fs::read_to_string(path).ok().map(|value| value.trim().to_string()).filter(|value| !value.is_empty())
}

fn chip_kind(name: &str) -> &'static str {
    match name {
        "coretemp" | "k10temp" | "zenpower" | "cpu_thermal" => "cpu",
        "nvme" => "nvme",
        "amdgpu" | "nvidia" => "gpu",
        "acpitz" => "chassis",
        _ => "other",
    }
}

fn read_chip(dir: &Path) -> Vec<SensorReading> {
    let Some(hwmon) = dir.file_name().and_then(|value| value.to_str()).map(str::to_string) else { return Vec::new() };
    let Some(name) = read_trimmed(&dir.join("name")) else { return Vec::new() };
    let kind = chip_kind(&name);
    let device = read_trimmed(&dir.join("device")) .unwrap_or_default();
    let kernel = read_trimmed(&dir.join("device/uevent"))
        .and_then(|value| value.lines().find(|line| line.starts_with("DRIVER=")).map(str::to_string))
        .unwrap_or_default();
    let mut readings = Vec::new();
    let Ok(entries) = fs::read_dir(dir) else { return readings };
    for entry in entries.flatten() {
        let path = entry.path();
        let Some(file) = path.file_name().and_then(|value| value.to_str()) else { continue };
        let Some(index) = file.strip_prefix("temp").and_then(|value| value.strip_suffix("_input")) else { continue };
        if index.is_empty() || !index.chars().all(|value| value.is_ascii_digit()) { continue }
        let Ok(millidegrees) = read_trimmed(&path).and_then(|value| value.parse::<i64>().ok()).ok_or(()) else { continue };
        let temp_c = millidegrees.div_euclid(1000);
        if !(-100..=200).contains(&temp_c) { continue }
        let label = read_trimmed(&dir.join(format!("temp{index}_label"))).unwrap_or_else(|| format!("temp{index}"));
        readings.push(SensorReading { kind, hwmon: hwmon.clone(), device: device.clone(), kernel: kernel.clone(), label, temp_c: temp_c as i32 });
    }
    readings
}

pub fn read_all() -> Vec<SensorReading> {
    let mut readings = Vec::new();
    let Ok(entries) = fs::read_dir("/sys/class/hwmon") else { return readings };
    for entry in entries.flatten() {
        readings.extend(read_chip(&entry.path()));
    }
    readings.sort_by(|a, b| a.hwmon.cmp(&b.hwmon).then(a.label.cmp(&b.label)));
    readings
}

pub fn primary_temperature(readings: &[SensorReading]) -> Option<i32> {
    readings.iter().find(|reading| reading.label == "Package id 0").map(|reading| reading.temp_c)
        .or_else(|| readings.iter().map(|reading| reading.temp_c).max())
}
