//! Bounded host-side thermal controller. HID execution remains outside this module.

use crate::curve::{target_rpm, CurvePolicy, HYSTERESIS_C};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ControllerState {
    pub last_temp_c: Option<i32>,
    pub last_rpm: u16,
}

impl Default for ControllerState {
    fn default() -> Self { Self { last_temp_c: None, last_rpm: 1000 } }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Decision {
    Hold,
    Set(u16),
}

/// Decide a non-zero target from a valid host temperature.
/// A missing sensor never produces a stop command: caller must keep the last target.
pub fn decide(state: &mut ControllerState, policy: &CurvePolicy, temp_c: Option<i32>, supply_max_rpm: u16) -> Decision {
    let Some(temp_c) = temp_c else { return Decision::Hold };
    if let Some(previous) = state.last_temp_c {
        if (temp_c - previous).abs() < HYSTERESIS_C && state.last_rpm != 0 {
            return Decision::Hold;
        }
    }
    let rpm = target_rpm(policy, temp_c, supply_max_rpm).max(500);
    state.last_temp_c = Some(temp_c);
    if rpm == state.last_rpm { Decision::Hold } else { state.last_rpm = rpm; Decision::Set(rpm) }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::curve::community_default;

    #[test]
    fn missing_sensor_holds_safe_target() {
        let mut state = ControllerState::default();
        assert_eq!(decide(&mut state, &community_default("performance"), None, 2700), Decision::Hold);
        assert_eq!(state.last_rpm, 1000);
    }

    #[test]
    fn curve_is_supply_clamped_and_nonzero() {
        let mut state = ControllerState::default();
        let policy = community_default("performance");
        assert_eq!(decide(&mut state, &policy, Some(100), 2700), Decision::Set(2700));
    }

    #[test]
    fn small_temperature_changes_hold() {
        let mut state = ControllerState::default();
        let policy = community_default("performance");
        assert_eq!(decide(&mut state, &policy, Some(60), 4000), Decision::Set(2300));
        assert_eq!(decide(&mut state, &policy, Some(61), 4000), Decision::Hold);
    }
}
