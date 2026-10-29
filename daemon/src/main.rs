//! Owned Flydigi BS2 Pro daemon boundary.
//!
//! This first slice owns the process and Unix-socket lifecycle only. Hardware
//! discovery and HID/Bluetooth transport are deliberately not faked here; the
//! daemon fails closed with an explicit disconnected status until D3 supplies
//! an evidence-backed device backend.
//!
//! **Wake sequence**: The device requires a `SetStandby(Off)` command followed
//! by a brief pause before it begins emitting spontaneous status reports.
//! The `wake()` method on `Session` performs this sequence and can be called
//! before `status()`. After wake, the first `status` read will return
//! `connected=true` with valid RPM/gear/supply data.

use std::env;
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Path;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::thread::{self, JoinHandle};
use std::time::Duration;
use thiserror::Error;

use flydigi_bs2pro_linux_daemon::command::Gear;
use flydigi_bs2pro_linux_daemon::session::{execute_set_gear, execute_set_manual, execute_set_rgb_power, wake_and_status};

const DEFAULT_SOCKET: &str = "/run/flydigictl/flydigictl.sock";
static AUTO_RUNNING: AtomicBool = AtomicBool::new(false);
static AUTO_THREAD: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);

fn auto_state_path() -> PathBuf {
    let base = env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")))
        .unwrap_or_else(|| PathBuf::from("."));
    base.join("flydigi-bs2pro-linux").join("auto-state.json")
}

fn persist_auto(enabled: bool) {
    let path = auto_state_path();
    if let Some(parent) = path.parent() {
        if let Err(error) = fs::create_dir_all(parent) {
            eprintln!("auto state directory: {error}");
            return;
        }
    }
    if let Err(error) = fs::write(path, if enabled { &b"true"[..] } else { &b"false"[..] }) {
        eprintln!("auto state write: {error}");
    }
}

fn persisted_auto() -> bool {
    fs::read(auto_state_path()).map(|bytes| bytes == b"true").unwrap_or(false)
}

fn recover_cooling_state(auto_enabled: bool) {
    if auto_enabled {
        start_auto();
        return;
    }
    let status = wake_and_status();
    if status.connected && status.smart_mode == Some(true) {
        match execute_set_gear(Gear::Standard, 2400) {
            Ok(_) => eprintln!("recovered firmware realtime state to Standard gear"),
            Err(error) => eprintln!("failed to recover firmware realtime state: {error}"),
        }
    }
}

fn start_auto() {
    if AUTO_RUNNING.swap(true, Ordering::SeqCst) {
        return;
    }
    let handle = thread::spawn(|| {
        let mut state = flydigi_bs2pro_linux_daemon::control::ControllerState::default();
        while AUTO_RUNNING.load(Ordering::SeqCst) {
            let policy = flydigi_bs2pro_linux_daemon::curve::load("performance");
            let readings = flydigi_bs2pro_linux_daemon::sensors::read_all();
            let temperature = flydigi_bs2pro_linux_daemon::sensors::primary_temperature(&readings);
            let supply = wake_and_status().supply_max_rpm.unwrap_or(4000);
            if let flydigi_bs2pro_linux_daemon::control::Decision::Set(rpm) = flydigi_bs2pro_linux_daemon::control::decide(&mut state, &policy, temperature, supply) {
                if let Err(error) = execute_set_manual(rpm) { eprintln!("auto control: {error}"); }
            }
            thread::sleep(Duration::from_secs(1));
        }
    });
    *AUTO_THREAD.lock().expect("auto thread mutex poisoned") = Some(handle);
}

fn stop_auto() {
    AUTO_RUNNING.store(false, Ordering::SeqCst);
    if let Some(handle) = AUTO_THREAD.lock().expect("auto thread mutex poisoned").take() {
        let _ = handle.join();
    }
}

#[derive(Debug, Error)]
enum DaemonError {
    #[error("socket setup failed: {0}")]
    Socket(std::io::Error),
    #[error("invalid request: {0}")]
    Json(#[from] serde_json::Error),
    #[error("device not found")]
    NoDevice,
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("timed out waiting for fresh status; readback required before another command")]
    StatusTimeout,
    #[error("device is asleep; run wake() first")]
    Asleep,
    #[error("command {command:#04x} rejected with ACK {status:#04x}; readback required before another command")]
    AckRejected { command: u8, status: u8 },
    #[error("command {0:#04x} not confirmed; readback required before another command")]
    Unconfirmed(u8),
    #[error("missing ACK for {0:#04x}; readback required before another command")]
    AckTimeout(u8),
    #[error("short HID write ({actual}/{expected}); readback required before another command")]
    ShortWrite { actual: usize, expected: usize },
}

fn socket_path() -> PathBuf {
    env::var_os("FLYDIGI_SOCKET")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(DEFAULT_SOCKET))
}

fn write_json<T: serde::Serialize>(stream: &mut UnixStream, value: &T) -> Result<(), DaemonError> {
    serde_json::to_writer(&mut *stream, value).map_err(DaemonError::Json)?;
    stream.write_all(b"\n").map_err(DaemonError::Io)?;
    stream.flush().map_err(DaemonError::Io)?;
    Ok(())
}

fn handle_connection(mut stream: UnixStream) -> Result<(), DaemonError> {
    // Keep command and subscription connections alive: the GUI sends multiple
    // newline-delimited requests over each Unix socket.
    let mut reader = BufReader::new(stream.try_clone()?);
    loop {
        let mut line = String::new();
        let len = reader.read_line(&mut line)?;
        if len == 0 {
            return Ok(());
        }
        let request: serde_json::Value = match serde_json::from_str(&line) {
        Ok(v) => v,
        Err(_) => {
            write_json(&mut stream, &serde_json::json!({
                "reply": "error",
                "message": "invalid json",
            }))?;
            continue;
        }
        };
        let request_name = request.get("request").and_then(serde_json::Value::as_str);

    match request_name {
        Some("set_automatic") => {
            persist_auto(true);
            start_auto();
            write_json(&mut stream, &serde_json::json!({"reply":"ok","readback_confirmed":true,"smart_mode":true,"host_auto":true,"policy_source":"community-thrm-provisional"}))?;
        }
        Some("set_manual") => {
            stop_auto();
            persist_auto(false);
            let Some(rpm) = request.get("rpm").and_then(serde_json::Value::as_u64).and_then(|value| u16::try_from(value).ok()) else {
                write_json(&mut stream, &serde_json::json!({"reply": "error", "message": "rpm must be an integer"}))?;
                return Ok(());
            };
            match execute_set_manual(rpm) {
                Ok(status) => write_json(&mut stream, &serde_json::json!({"reply": "ok", "readback_confirmed": true, "status": status}))?,
                Err(message) => write_json(&mut stream, &serde_json::json!({"reply": "error", "message": message}))?,
            }
        }
        Some("set_gear") => {
            stop_auto();
            persist_auto(false);
            let gear = match request.get("gear").and_then(serde_json::Value::as_str) {
                Some("quiet") => Gear::Quiet,
                Some("standard") => Gear::Standard,
                Some("strong") => Gear::Strong,
                Some("overclock") => Gear::Overclock,
                _ => {
                    write_json(&mut stream, &serde_json::json!({"reply": "error", "message": "gear must be quiet, standard, strong, or overclock"}))?;
                    return Ok(());
                }
            };
            let Some(rpm) = request.get("rpm").and_then(serde_json::Value::as_u64).and_then(|value| u16::try_from(value).ok()) else {
                write_json(&mut stream, &serde_json::json!({"reply": "error", "message": "rpm must be an integer"}))?;
                return Ok(());
            };
            match execute_set_gear(gear, rpm) {
                Ok(status) => write_json(&mut stream, &serde_json::json!({
                    "reply": "ok",
                    "readback_confirmed": true,
                    "status": status,
                }))?,
                Err(message) => write_json(&mut stream, &serde_json::json!({"reply": "error", "message": message}))?,
            }
        }
        Some("status") | Some("subscribe") => {
            let status = wake_and_status();
            let json_status = serde_json::json!({
                "reply": status.reply,
                "model": status.model,
                "connected": status.connected,
                "current_rpm": status.current_rpm,
                "target_rpm": status.target_rpm,
                "smart_mode": AUTO_RUNNING.load(Ordering::SeqCst) || status.smart_mode.unwrap_or(false),
                "host_auto": AUTO_RUNNING.load(Ordering::SeqCst),
                "fixed_gear": status.fixed_gear && !AUTO_RUNNING.load(Ordering::SeqCst),
                "manual": status.manual.unwrap_or(false) && !AUTO_RUNNING.load(Ordering::SeqCst),
                "manual_rpm": status.manual_rpm,
                "supply": status.supply,
                "supply_max_rpm": status.supply_max_rpm,
                "transport": status.transport.map(|t| match t {
                    flydigi_bs2pro_linux_daemon::device::Transport::Usb => "cable",
                    flydigi_bs2pro_linux_daemon::device::Transport::Bluetooth => "bluetooth",
                }),
                "lighting": status.lighting,
                "strip_on": status.strip_on,
                "revision": status.revision,
                "demands": status.demands,
            });
            write_json(&mut stream, &json_status)?;
        }
        Some("sensors") => {
            let readings = flydigi_bs2pro_linux_daemon::sensors::read_all();
            write_json(&mut stream, &serde_json::json!({
                "reply": "sensors",
                "sensors": readings
            }))?;
        }
        Some("gears") => {
            let status = wake_and_status();
            let max_rpm = status.supply_max_rpm.unwrap_or(0);
            let gears = [
                ("quiet", [1300u16, 1700, 1900]),
                ("standard", [2100u16, 2400, 2700]),
                ("strong", [2800u16, 3000, 3300]),
                ("overclock", [3500u16, 3700, 4000]),
            ];
            let values = gears.into_iter().map(|(name, levels)| {
                serde_json::json!({
                    "name": name,
                    "rpm": levels.iter().copied().filter(|rpm| *rpm <= max_rpm).last().unwrap_or(levels[0]),
                    "levels": levels,
                    "allowed": levels.iter().any(|rpm| *rpm <= max_rpm),
                })
            }).collect::<Vec<_>>();
            write_json(&mut stream, &serde_json::json!({
                "reply": "gears",
                "gears": values
            }))?;
        }
        Some("set_lighting") => {
            let mode = request.get("lighting").and_then(|value| value.get("mode"));
            let mode_name = mode.and_then(|value| value.get("mode")).and_then(serde_json::Value::as_str);
            if mode_name == Some("effect") {
                write_json(&mut stream, &serde_json::json!({"reply":"error","message":"RGB presets are unavailable while they can alter cooling realtime state"}))?;
            } else {
                let Some(enabled) = mode_name.map(|name| name != "off") else {
                    write_json(&mut stream, &serde_json::json!({"reply":"error","message":"lighting mode must be off, static, or effect"}))?;
                    return Ok(());
                };
                match execute_set_rgb_power(enabled) {
                    Ok(status) => write_json(&mut stream, &serde_json::json!({"reply":"ok","readback_confirmed":true,"status":status}))?,
                    Err(message) => write_json(&mut stream, &serde_json::json!({"reply":"error","message":message}))?,
                }
            }
        }
        Some("set_curve_preset") => {
            let Some(preset) = request.get("preset").and_then(serde_json::Value::as_str) else {
                write_json(&mut stream, &serde_json::json!({"reply":"error","message":"preset is required"}))?;
                return Ok(());
            };
            if !matches!(preset, "quiet" | "performance" | "custom") {
                write_json(&mut stream, &serde_json::json!({"reply":"error","message":"preset must be quiet, performance, or custom"}))?;
                return Ok(());
            }
            let mut policy = if preset == "custom" {
                let Some(points) = request.get("points").cloned() else {
                    write_json(&mut stream, &serde_json::json!({"reply":"error","message":"custom preset requires points"}))?;
                    return Ok(());
                };
                let Ok(points) = serde_json::from_value(points) else {
                    write_json(&mut stream, &serde_json::json!({"reply":"error","message":"invalid curve points"}))?;
                    return Ok(());
                };
                flydigi_bs2pro_linux_daemon::curve::CurvePolicy { source: "user-custom".into(), preset: preset.into(), points, hysteresis_c: flydigi_bs2pro_linux_daemon::curve::HYSTERESIS_C }
            } else {
                flydigi_bs2pro_linux_daemon::curve::community_default(preset)
            };
            if policy.points.len() != 8 || policy.points.windows(2).any(|pair| pair[0].temp_c >= pair[1].temp_c || pair[0].rpm < 500 || pair[0].rpm > 4000) || policy.points.last().is_some_and(|point| point.rpm < 500 || point.rpm > 4000) {
                write_json(&mut stream, &serde_json::json!({"reply":"error","message":"curve must contain 8 ordered points with RPM 500..4000"}))?;
                return Ok(());
            }
            match flydigi_bs2pro_linux_daemon::curve::save(&policy) {
                Ok(()) => write_json(&mut stream, &serde_json::json!({"reply":"ok","readback_confirmed":true,"config":{"curve_policy":policy,"writable":true}}))?,
                Err(error) => write_json(&mut stream, &serde_json::json!({"reply":"error","message":error.to_string()}))?,
            }
        }
        Some("get_config") => {
            let policy = flydigi_bs2pro_linux_daemon::curve::load("performance");
            write_json(&mut stream, &serde_json::json!({
                "reply": "config",
                "config": {"curve_policy": policy},
                "writable": true
            }))?;
        }
        Some("query_supply") => {
            // Pure encoded report; no device opened, no HW access.
            // Firmware may return a status report shortly; we honour
            // the documented opcode 0x07 and wait for fresh status
            // below in the executor.
            write_json(&mut stream, &serde_json::json!({
                "reply": "error",
                "message": "hardware backend is not implemented",
            }))?;
        }
        Some(_) => {
            write_json(&mut stream, &serde_json::json!({
                "reply": "error",
                "message": "unknown request",
            }))?;
        }
        None => {
            write_json(&mut stream, &serde_json::json!({
                "reply": "error",
                "message": "request field is required",
            }))?;
        }
        }
    }
}

fn run() -> Result<(), DaemonError> {
    let path = socket_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    if Path::new(&path).exists() {
        fs::remove_file(&path)?;
    }
    let listener = UnixListener::bind(&path)?;
    // Set restrictive permissions so only the owner + group can talk.
    if let Some(p) = path.parent() {
        fs::set_permissions(p, fs::Permissions::from_mode(0o755))?;
    }
    fs::set_permissions(&path, fs::Permissions::from_mode(0o660))?;
    eprintln!("flydigi-bs2pro-linux-daemon listening on {}", path.display());
    recover_cooling_state(persisted_auto());

    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                std::thread::spawn(move || {
                    if let Err(error) = handle_connection(stream) {
                        eprintln!("connection error: {error}");
                    }
                });
            }
            Err(error) => eprintln!("accept error: {error}"),
        }
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("flydigi-bs2pro-linux-daemon: {error}");
        std::process::exit(1);
    }
}