# OEE Monitoring  Desktop Application


## 💻 Development Workflow

If you want to run the Desktop Application in development mode:

### 1. Run the Python Backend
Navigate to the backend directory and start the FastAPI server:
```bash
cd Desktop_Application/backend
# (Optional) Install dependencies
pip install -r requirements.txt
# Run the backend
python main.py
```
The backend server runs locally on `http://127.0.0.1:8000`.

### 2. Run the Frontend (with Tauri)
Navigate to the frontend directory and start the Tauri dev environment:
```bash
cd Desktop_Application/oee-ui
# Install node packages
npm install
# Run the to verify UI
npm run dev
```

---

## 🚀 Production Build Guide (Tauri App)

Follow these steps to package and build the production-ready Desktop Application on Windows.

### ✅ STEP 0 — Prerequisites (Install Once)

Ensure you have the following installed on your machine:

1. **Node.js**
   ```bash
   node -v
   ```
2. **Rust & Cargo**
   ```bash
   rustc --version
   cargo --version
   ```
   *If not installed, download from [rustup.rs](https://rustup.rs).*
3. **Python**
   ```bash
   python --version
   ```
4. **PyInstaller**
   ```bash
   pip install pyinstaller
   ```

---

### ✅ STEP 1 — Build the Python Backend

Go to the Python backend folder (where `main.py` exists):
```bash
cd Desktop_Application/backend
```

Build the standalone executable:
```bash
python -m PyInstaller main.py --onefile --name production-backend
```

After a successful build, you will find the executable inside:
`Desktop_Application/backend/dist/production-backend.exe`

---

### ✅ STEP 2 — Rename for Tauri v2 (Target Architecture Suffix)

Tauri v2 requires the sidecar executable to include the target architecture suffix.

1. Find your Rust target host:
   ```bash
   rustc -vV
   ```
   You will see an output like:
   `host: x86_64-pc-windows-msvc`
2. Rename the backend executable to match this suffix:
   * **Original**: `production-backend.exe`
   * **Renamed**: `production-backend-x86_64-pc-windows-msvc.exe`

---

### ✅ STEP 3 — Move the Backend into Tauri

Create the `backend` folder inside Tauri's source folder (if it doesn't already exist):
```bash
mkdir -p Desktop_Application/oee-ui/src-tauri/backend
```

Move the renamed executable into this folder:
`Desktop_Application/oee-ui/src-tauri/backend/production-backend-x86_64-pc-windows-msvc.exe`

**Final Directory Structure:**
```
Desktop_Application/
  oee-ui/
    src-tauri/
      backend/
        production-backend-x86_64-pc-windows-msvc.exe
```

---

### ✅ STEP 4 — Configure `tauri.conf.json`

Verify the configuration inside `Desktop_Application/oee-ui/src-tauri/tauri.conf.json`.

Ensure it contains:
```json
"build": {
  "frontendDist": "../dist",
  "devUrl": "http://localhost:5173",
  "beforeDevCommand": "npm run dev",
  "beforeBuildCommand": "npm run build"
},
"bundle": {
  "active": true,
  "targets": "all",
  "externalBin": [
    "backend/production-backend"
  ]
}
```
> [!WARNING]
> * Do **NOT** include `.exe` in the `externalBin` config.
> * Do **NOT** include the architecture suffix (e.g., `-x86_64-pc-windows-msvc`) in the config.

---

### ✅ STEP 5 — Configure `Cargo.toml`

Inside `Desktop_Application/oee-ui/src-tauri/Cargo.toml`, ensure `reqwest` with the `blocking` feature is added under `[dependencies]`:
```toml
[dependencies]
reqwest = { version = "0.11", features = ["blocking"] }
```

---

### ✅ STEP 6 — Verify `main.rs`

Verify that `Desktop_Application/oee-ui/src-tauri/src/main.rs` contains the logic to locate the backend and shutdown properly:

1. **Locate backend process:**
   ```rust
   let backend_dir = exe_dir.join("backend");
   let backend_exe = backend_dir.join("production-backend.exe");
   ```
2. **Shutdown endpoint request on exit:**
   ```rust
   let _ = Client::new()
       .post("http://127.0.0.1:8000/shutdown")
       .send();
   ```

---

### ✅ STEP 7 — Build React Frontend

Navigate to the frontend directory:
```bash
cd Desktop_Application/oee-ui
```

Install packages and compile the production web build:
```bash
npm install
npm run build
```
This generates the static files inside `Desktop_Application/oee-ui/dist/`.

---

### ✅ STEP 8 — Build Tauri Desktop Application

From the `Desktop_Application/oee-ui/` directory, run:
```bash
npx tauri build
```

---

### ✅ STEP 9 — Output Location

After a successful compilation, your installers will be generated under:
`Desktop_Application/oee-ui/src-tauri/target/release/bundle/`

You will find:
* A `.msi` Windows Installer
* A `.exe` Setup installer

---

### ✅ STEP 10 — Verification and Testing

1. Run the installer to install the application.
2. Launch the application.
3. Open **Task Manager** and confirm `production-backend.exe` is running under background processes.
4. Close the application window.
5. Confirm in **Task Manager** that the `production-backend.exe` process stops immediately after closing the app.