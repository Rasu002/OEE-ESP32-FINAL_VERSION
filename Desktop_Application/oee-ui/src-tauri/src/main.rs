#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::{Command, Child};
use std::env;
use std::thread;
use std::time::Duration;
use std::sync::{Arc, Mutex};

use reqwest::blocking::Client;

#[cfg(target_os = "windows")]
use std::os::windows::process::CommandExt;

fn main() {
    let exe_dir = env::current_exe().unwrap().parent().unwrap().to_path_buf();

    // backend located beside app.exe
    let backend_exe = exe_dir.join("production-backend.exe");

    let backend_child: Arc<Mutex<Option<Child>>> = Arc::new(Mutex::new(None));

    if backend_exe.exists() {
        let mut cmd = Command::new(&backend_exe);

        #[cfg(target_os = "windows")]
        {
            cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
        }

        let child = cmd.spawn().expect("Backend failed");
        *backend_child.lock().unwrap() = Some(child);
    }

    thread::sleep(Duration::from_secs(3));

    let backend_child_clone = backend_child.clone();

    tauri::Builder::default()
        .on_window_event(move |_window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();

                let _ = Client::new()
                    .post("http://127.0.0.1:8000/shutdown")
                    .send();

                thread::sleep(Duration::from_secs(2));

                if let Some(mut child) = backend_child_clone.lock().unwrap().take() {
                    let _ = child.wait();
                }

                std::process::exit(0);
            }
        })
        .run(tauri::generate_context!())
        .expect("error running tauri app");
}