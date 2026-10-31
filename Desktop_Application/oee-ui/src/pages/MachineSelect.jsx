import { useEffect, useState } from "react";
import { useMachine } from "../context/MachineContext";
import { API_BASE } from "../services/api";
import logo from "../assets/icon.png";
import "./machine-select.css";

const MACHINE_STATUS_URL = `${API_BASE}/machines/status`;
const MACHINE_REFRESH_MS = 1000;

export default function MachineSelect({ onSelect, onSettings }) {
  const { setMachine } = useMachine();
  const [machines, setMachines] = useState([]);
  const [loadError, setLoadError] = useState("");
  const [now, setNow] = useState(new Date());

  useEffect(() => {
    let ignore = false;
    let controller;

    const loadMachines = async () => {
      try {
        // cancel previous request
        if (controller) controller.abort();
        controller = new AbortController();

        const response = await fetch(MACHINE_STATUS_URL, {
          cache: "no-store", // 🔥 no caching
          signal: controller.signal
        });

        if (!response.ok) {
          throw new Error(`API error ${response.status}`);
        }

        const data = await response.json();

        if (!ignore) {
          setMachines(Array.isArray(data) ? data : []);
          setLoadError("");
        }

      } catch (error) {
        if (error.name !== "AbortError" && !ignore) {
          setLoadError("Unable to load machines.");
        }
      }
    };

    loadMachines();

    // 🔥 increase interval
    const statusTimer = setInterval(loadMachines, 3000);

    const clockTimer = setInterval(() => setNow(new Date()), 2000);

    return () => {
      ignore = true;
      if (controller) controller.abort();
      clearInterval(statusTimer);
      clearInterval(clockTimer);
    };
  }, []);

  function selectMachine(machine) {
    setMachine({
      machine_id: machine.machine_id,
      machine_name: machine.machine_name,
    });
    onSelect();
  }

  return (
    <div className="machine-select-page">
      <div className="machine-top-bar">
        <div className="brand">
          <img src={logo} alt="logo" className="brand-logo" />
          SWAY AUTOMATION AND TECHNOLOGIES
        </div>

        <div className="top-right">
          <span className="time">
            {now.toLocaleDateString()} | {now.toLocaleTimeString()}
          </span>
          <button className="settings-btn" onClick={onSettings}>
            Settings
          </button>
        </div>
      </div>

      <div className="machine-content">
        <h2>Select a Machine</h2>

        {loadError && <div className="machine-empty">{loadError}</div>}

        {!loadError && machines.length === 0 && (
          <div className="machine-empty">
            No active machines found. Add a machine in Settings and keep it active.
          </div>
        )}

        <div className="machine-grid">
          {machines.map((machine) => (
            <div
              key={machine.machine_id}
              className={`machine-card ${isOfflineMachine(machine.plc_status) ? "is-offline" : ""}`}
              onClick={() => selectMachine(machine)}
            >
              <div className="machine-card-header">
                <div className="machine-name">{machine.machine_name}</div>
                <span className={`plc-badge ${plcClass(machine.plc_status)}`}>
                  {displayPlcStatus(machine.plc_status)}
                </span>
              </div>

              <div className="machine-plc">IP: {machine.plc_address || "Not Configured"}</div>

              <span className={`status-badge ${statusClass(machine.machine_state ?? machine.status)}`}>
                {displayStatus(machine.machine_state ?? machine.status)}
              </span>

              <div className="view-hint">Click to open</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

function isOfflineMachine(plcStatus = "") {
  return plcStatus === "OFFLINE" || plcStatus === "NOT_CONFIGURED";
}

function plcClass(plcStatus = "") {
  switch (plcStatus) {
    case "ONLINE":
      return "online";
    case "OFFLINE":
      return "offline";
    case "NOT_CONFIGURED":
      return "unknown";
    default:
      return "unknown";
  }
}

function displayPlcStatus(plcStatus = "") {
  switch (plcStatus) {
    case "ONLINE":
      return "CONNECTED";
    case "OFFLINE":
      return "DISCONNECTED";
    case "NOT_CONFIGURED":
      return "NO IP";
    default:
      return plcStatus || "UNKNOWN";
  }
}

function displayStatus(status = "") {
  switch (status) {
    case "PLC_ONLINE":
      return "ONLINE";
    case "PLC_OFFLINE":
    case "MCU_OFFLINE":
      return "OFFLINE";
    case "RUN":
    case "RUNNING":
      return "RUN";
    case "IDLE":
      return "IDLE";
    case "FAULT":
      return "FAULT";
    case "STOP":
    case "STOPPED":
      return "STOP";
    default:
      return status || "UNKNOWN";
  }
}

function statusClass(status = "") {
  switch (status) {
    case "RUN":
    case "RUNNING":
      return "run";
    case "IDLE":
      return "idle";
    case "FAULT":
      return "fault";
    case "STOP":
    case "STOPPED":
      return "stop";
    case "PLC_OFFLINE":
    case "MCU_OFFLINE":
      return "offline";
    default:
      return "unknown";
  }
}
