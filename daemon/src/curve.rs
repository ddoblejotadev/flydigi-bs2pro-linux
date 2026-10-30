//! Explicit provisional host-side cooling policy.
//! Values are based on the public THRM community baseline, not Flydigi defaults.

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

pub const HYSTERESIS_C: i32 = 2;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct CurvePoint {
    pub temp_c: i32,
    pub rpm: u16,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct CurvePolicy {
    pub source: String,
    pub preset: String,
    pub points: Vec<CurvePoint>,
    pub hysteresis_c: i32,
}

pub fn community_default(preset: &str) -> CurvePolicy {
    CurvePolicy {
        source: "community-thrm-provisional".into(),
        preset: preset.into(),
        points: vec![
            CurvePoint { temp_c: 30, rpm: 1000 },
            CurvePoint { temp_c: 40, rpm: 1400 },
            CurvePoint { temp_c: 50, rpm: 1800 },
            CurvePoint { temp_c: 60, rpm: 2300 },
            CurvePoint { temp_c: 70, rpm: 2900 },
            CurvePoint { temp_c: 80, rpm: 3500 },
            CurvePoint { temp_c: 90, rpm: 4000 },
            CurvePoint { temp_c: 100, rpm: 4000 },
        ],
        hysteresis_c: HYSTERESIS_C,
    }
}

pub fn config_path() -> PathBuf {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")))
        .unwrap_or_else(|| PathBuf::from("."));
    base.join("flydigi-bs2pro-linux").join("curve-policy.json")
}

pub fn load(preset: &str) -> CurvePolicy {
    fs::read(config_path()).ok()
        .and_then(|bytes| serde_json::from_slice::<CurvePolicy>(&bytes).ok())
        .filter(|policy| policy.points.len() == 8 && policy.preset == preset)
        .unwrap_or_else(|| community_default(preset))
}

/// Load the user-selected curve for the host-side Auto controller.
/// Unlike `load`, this does not force a requested preset and therefore preserves
/// the Quiet/Performance/Custom selection made by the GUI.
pub fn load_active() -> CurvePolicy {
    fs::read(config_path()).ok()
        .and_then(|bytes| serde_json::from_slice::<CurvePolicy>(&bytes).ok())
        .filter(|policy| policy.points.len() == 8)
        .unwrap_or_else(|| community_default("performance"))
}

pub fn save(policy: &CurvePolicy) -> std::io::Result<()> {
    let path = config_path();
    if let Some(parent) = path.parent() { fs::create_dir_all(parent)?; }
    let bytes = serde_json::to_vec_pretty(policy).map_err(std::io::Error::other)?;
    fs::write(path, bytes)
}

pub fn target_rpm(policy: &CurvePolicy, temp_c: i32, supply_max_rpm: u16) -> u16 {
    let points = &policy.points;
    if points.is_empty() { return 0; }
    let mut rpm = points[0].rpm as i32;
    if temp_c >= points[points.len() - 1].temp_c {
        rpm = points[points.len() - 1].rpm as i32;
    } else {
        for pair in points.windows(2) {
            if temp_c <= pair[1].temp_c {
                let left = &pair[0];
                let right = &pair[1];
                let span = (right.temp_c - left.temp_c).max(1);
                rpm = left.rpm as i32 + (right.rpm as i32 - left.rpm as i32)
                    * (temp_c - left.temp_c).clamp(0, span) / span;
                break;
            }
        }
    }
    rpm.clamp(500, supply_max_rpm as i32).try_into().unwrap_or(supply_max_rpm)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn baseline_has_eight_bounded_points() {
        let policy = community_default("performance");
        assert_eq!(policy.points.len(), 8);
        assert_eq!(target_rpm(&policy, 30, 4000), 1000);
        assert_eq!(target_rpm(&policy, 65, 4000), 2600);
        assert_eq!(target_rpm(&policy, 100, 2700), 2700);
    }
}
