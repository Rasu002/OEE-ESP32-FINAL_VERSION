import { useState, useEffect } from "react";
import { useMachine } from "../context/MachineContext";
import logo from "../assets/icon.png";

import Overview from "../pages/Overview";
import Events from "../pages/Events";
import Downtime from "../pages/Downtime";
import Faults from "../pages/Faults";
import Settings from "../pages/Settings";
import "./appstyle.css";

export default function AppLayout({ onChangeMachine, forceSettings, onExitSettings }) {
  const [page, setPage] = useState(forceSettings ? "settings" : "overview");
  const hideNav = forceSettings || page === "settings";
  const { machine } = useMachine();
  const [now, setNow] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  return (
    <div
      style={{
        height: "100%",
        width: "100%",
        display: "flex",
        flexDirection: "column"
      }}
    >
      {/* ===== TITLE BAR (ONLY ONCE) ===== */}
      <div className="title-bar">
        <div className="title-text">
          <img src={logo} alt="logo" className="title-logo" />
          SWAY AUTOMATION AND TECHNOLOGIES
        </div>

        <div className="title-time">
          {now.toLocaleDateString(undefined, {
            day: "2-digit",
            month: "short",
            year: "numeric"
          })} •{" "}
          {now.toLocaleTimeString()}
        </div>
      </div>

      {/* ===== MAIN NAV BAR ===== */}
      {!hideNav && (
        <div className="main-bar">

          {/* LEFT : PAGE NAV */}
          <div className="main-nav">
            <button
              className={page === "overview" ? "active" : ""}
              onClick={() => setPage("overview")}
            >
              Overview
            </button>

            <button
              className={page === "events" ? "active" : ""}
              onClick={() => setPage("events")}
            >
              Events
            </button>

            {/* <button
              className={page === "downtime" ? "active" : ""}
              onClick={() => setPage("downtime")}
            >
              Downtime
            </button> */}

            <button
              className={page === "faults" ? "active" : ""}
              onClick={() => setPage("faults")}
            >
              Faults
            </button>
          </div>

          {/* RIGHT : MACHINE + SETTINGS */}
          <div className="main-right">
            <button
              className="machine-glow-btn"
              onClick={onChangeMachine}
              title="Change machine"
            >
              {machine ? (
                <>
                  <span className="machine-name">{machine.machine_name}</span>
                  <span className="machine-id">ID {machine.machine_id}</span>
                </>
              ) : (
                "No machine selected"
              )}
            </button>


            <button
              className="settings-btn"
              onClick={() => setPage("settings")}
            >
              ⚙ Settings
            </button>
          </div>

        </div>
      )}

      {/* ===== CONTENT ===== */}
      <main style={{ flex: 1, minHeight: 0, overflow: "hidden" }}>
        {page === "overview" && <Overview />}
        {page === "downtime" && <Downtime />}
        {page === "events" && <Events />}
        {page === "faults" && <Faults />}
        {page === "settings" && (
          <Settings
            onBack={() => {
              if (forceSettings) onExitSettings();
              else setPage("overview");
            }}
          />
        )}
      </main>
    </div>
  );
}
