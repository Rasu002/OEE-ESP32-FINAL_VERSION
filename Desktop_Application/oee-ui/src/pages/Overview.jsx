// Overview.jsx
import { useEffect, useState } from "react";
import { useMachine } from "../context/MachineContext";
import { syncRuntimeLive, getOEEHistory } from "../services/api";
import GaugeCard from "../components/GaugeCard";
import LiveMachineGraph from "../components/LiveMachineGraph";
import HistoryMachineGraph from "../components/HistoryMachineGraph";
import TotalVsPlannedGraph from "../components/TotalVsPlannedGraph";
import HistoryTotalVsPlannedGraph from "../components/HistoryTotalVsPlannedGraph";
import "./events.css";


export default function Overview() {

  const today = new Date().toISOString().split("T")[0];

  const yesterday = new Date(
    Date.now() - 24 * 60 * 60 * 1000
  )
    .toISOString()
    .split("T")[0];

  const { machine } = useMachine();
  const machineId = machine?.machine_id;

  const [live, setLive] = useState(null);
  const [loading, setLoading] = useState(true);
  const [oee, setOee] = useState({
    availability: 0, Error_time: 0, Idle_Time: 0, Utiliization_Rate: 0,
    runtime_sec: 0, planned_time_sec: 0,
    idle_sec: 0, fault_sec: 0,
    stop_count: 0, fault_count: 0
  });

  const [fromDate, setFromDate] = useState("");
  const [toDate, setToDate] = useState("");
  const [mode, setMode] = useState("LIVE"); // LIVE | HISTORY

  /* ================= DATA LOADER ================= */
  useEffect(() => {
    if (!machineId || mode !== "LIVE") return;

    setLoading(true);
    let cancelled = false;

    const fetchLive = async () => {
      try {
        const liveData = await syncRuntimeLive(machineId);
        // "stale" = ESP32 offline but cache has last-good values → still show it
        // "offline" = no cache at all (fresh start, ESP32 never reached) → keep zeros
        if (!cancelled && liveData && liveData.status !== "offline") {
          setLive(liveData);
          setOee(liveData);
        }
      } catch (_) {
        // Network error — keep last-good state shown in UI
      }
      if (!cancelled) setLoading(false);
    };

    fetchLive();
    const interval = setInterval(fetchLive, 60000);

    return () => {
      cancelled = true;
      clearInterval(interval);
    };
  }, [machineId, mode]);


  useEffect(() => {
    if (!machineId) return;

    if (mode === "HISTORY" && fromDate && toDate) {
      getOEEHistory(machineId, fromDate, toDate).then(setOee);
      setLive(null);
    }
  }, [machineId, mode, fromDate, toDate]);


  if (!machineId) return null;

  // if (!oee) {
  //   return <div style={{ padding: 20 }}>Loading OEE...</div>;
  // }

  return (
    <div className="events-page overview-page">

      {/* ===== TOP BAR ===== */}
      <div className="page-top-bar-over">

        <h3>Machine Performance</h3>
        <p style={{ paddingLeft: 20, fontSize: 12, color: "#6b7280" }}>
          {mode === "LIVE"
            ? "Live (Today)"
            : `Performance from ${fromDate} to ${toDate}`}
        </p>

        <div className="date-filter">
          <input
            type="date"
            value={fromDate}
            max={yesterday}
            onChange={e => {
              setFromDate(e.target.value);
              setMode("HISTORY");
            }}
          />

          <input
            type="date"
            value={toDate}
            max={yesterday}
            onChange={e => {
              setToDate(e.target.value);
              setMode("HISTORY");
            }}
          />

          <button
            className={`live-btn ${mode === "LIVE" ? "active" : ""}`}
            onClick={() => {
              setFromDate("");
              setToDate("");
              setMode("LIVE");
            }}
          >
            Live
          </button>
        </div>
      </div>

      {/* ===== LOADING ===== */}
      {mode === "LIVE" && loading && (
        <div className="live-loading">
          <div className="live-spinner" />
          <p className="live-loading-text">Connecting to machine…</p>
        </div>
      )}

      {/* ===== KPI ROW + MAIN CONTENT ===== */}
      {!(mode === "LIVE" && loading) && <>
      <div className="downtime-kpi-row">

        <GaugeCard
          title="Availability"
          value={(oee.availability * 100).toFixed(1)}
          unit="%"
          trendValue="Runtime / Planned"
          scale={0.8}
        />
        <GaugeCard
          title="Utilization  Rate"
          value={(oee.Utiliization_Rate * 100).toFixed(1)}
          unit="%"
          trendValue="Runtime / Total Time"
          scale={0.8}
        />


        <GaugeCard
          title="Power OFF Time"
          value={(() => {
            if (mode === "LIVE") {
              return (((oee.poweroff_sec ?? 0) / (oee.planned_time_sec || 1)) * 100).toFixed(1);
            }
            const poDays = oee.poweroff_available_days
              ?? (oee.poweroff_days?.filter(d => d.file_present).length ?? 0);
            const poPlan = oee.poweroff_planned_sec || (poDays > 0 ? null : 0);
            return (((oee.total_poweroff_sec ?? 0) / (poPlan || oee.planned_time_sec || 1)) * 100).toFixed(1);
          })()}
          unit="%"
          trendValue={(() => {
            if (mode === "LIVE") return "PowerOFF / Planned (Today)";
            const poDays = oee.poweroff_available_days
              ?? (oee.poweroff_days?.filter(d => d.file_present).length ?? 0);
            return poDays > 0
              ? `PowerOFF / Planned (${poDays} days)`
              : "PowerOFF / Planned (No CSV)";
          })()}
          scale={0.8}
        />

        {/* Time Breakdown card — donut chart */}
        <div className="gauge-card" style={{ "--gauge-scale": 0.8 }}>
          <div className="gauge-title">Time Breakdown</div>
          <div style={{ flex: 1, display: "flex", alignItems: "center", justifyContent: "center", gap: "calc(10px * var(--gauge-scale))" }}>
            {(() => {
              const total = oee.total_time || (oee.runtime_sec + oee.idle_sec + oee.fault_sec + oee.setting_sec) || 1;
              const segments = [
                { label: "Run",   sec: oee.runtime_sec, color: "#22c55e" },
                { label: "Idle",  sec: oee.idle_sec,    color: "#f59e0b" },
                { label: "Error", sec: oee.fault_sec,   color: "#ef4444" },
                { label: "Setting", sec: oee.setting_sec,   color: "#7e2d2dff" },
              ].map(s => ({ ...s, pct: Math.min(((s.sec || 0) / total) * 100, 100) }));

              // SVG donut via stroke-dasharray on a circle
              const R = 30, CX = 40, CY = 40;
              const CIRC = 2 * Math.PI * R;
              let offset = 0; // start from top (rotate -90deg on g)
              const slices = segments.map(({ label, pct, color }) => {
                const dash = (pct / 100) * CIRC;
                const gap  = CIRC - dash;
                const el = (
                  <circle
                    key={label}
                    cx={CX} cy={CY} r={R}
                    fill="none"
                    stroke={pct > 0 ? color : "none"}
                    strokeWidth={12}
                    strokeDasharray={`${dash} ${gap}`}
                    strokeDashoffset={-offset}
                    style={{ transition: "stroke-dasharray 0.4s ease" }}
                  />
                );
                offset += dash;
                return el;
              });

              return (
                <>
                  {/* Donut SVG */}
                  <svg width="calc(130px * var(--gauge-scale))" height="calc(130px * var(--gauge-scale))"
                    viewBox="0 0 80 80"
                    style={{ width: "calc(130px * var(--gauge-scale))", height: "calc(130px * var(--gauge-scale))", flexShrink: 0, transform: "rotate(-90deg)" }}>
                    <circle cx={CX} cy={CY} r={R} fill="none" stroke="#e5e7eb" strokeWidth={12} />
                    {slices}
                  </svg>
                  {/* Legend */}
                  <div style={{ display: "flex", flexDirection: "column", gap: "calc(5px * var(--gauge-scale))" }}>
                    {segments.map(({ label, pct, color }) => (
                      <div key={label} style={{ display: "flex", alignItems: "center", gap: "calc(5px * var(--gauge-scale))" }}>
                        <div style={{ width: "calc(8px * var(--gauge-scale))", height: "calc(8px * var(--gauge-scale))", borderRadius: "50%", background: color, flexShrink: 0 }} />
                        <span style={{ fontSize: "calc(15px * var(--gauge-scale))", color: "#6b7280", fontWeight: 500 }}>{label}</span>
                        <span style={{ fontSize: "calc(15px * var(--gauge-scale))", fontWeight: 700, color }}>{pct.toFixed(1)}%</span>
                      </div>
                    ))}
                  </div>
                </>
              );
            })()}
          </div>
          <div className="gauge-footer">
            <span className="trend-label">% of Total Time</span>
          </div>
        </div>
      </div>

      {/* ===== MAIN CONTENT ===== */}
      <div className="fault-main-overview">

        {mode === "LIVE" && live && (
          <div className="left-graphs">
            <LiveMachineGraph live={live} />
            <TotalVsPlannedGraph live={live} />
          </div>

        )}


        {mode === "HISTORY" && oee && (
          <div className="left-graphs">
            <HistoryMachineGraph
              oee={oee}
              machineId={machineId}
              fromDate={fromDate}
              toDate={toDate}
            />
            <HistoryTotalVsPlannedGraph live={oee} />
          </div>
        )}



        {/* ===== OEE BREAKDOWN ===== */}
        <div className="fault-table-box">

          {/* ===== LIVE → PRODUCTION PROGRESS ===== */}
          {mode === "LIVE" && live && (
            <>
              <h4>Live Machine Summary</h4>

              <table>

                <thead>
                  <tr>
                    <th>Metric</th>
                    <th>Value</th>
                  </tr>
                </thead>
                <tbody>

                  <tr>
                    <td>Total Run Time</td>
                    <td>{formatSeconds(live.runtime_sec)}</td>
                  </tr>

                  <tr>
                    <td>Total Idle Time</td>
                    <td>{formatSeconds(live.idle_sec)}</td>
                  </tr>

                  <tr>
                    <td>Total Error Time</td>
                    <td>{formatSeconds(live.fault_sec)}</td>
                  </tr>

                  <tr>
                    <td>Total Setting Time</td>
                    <td>{formatSeconds(live.setting_sec)}</td>
                  </tr>

                  {/* <tr>
      <td>Total Stops</td>
      <td>{live.stop_count ?? 0}</td>
    </tr> */}

                  <tr>
                    <td>Total Utilized Time</td>
                    <td>{formatSeconds(live.total_time || 0)}</td>
                  </tr>
                  <tr>
                    <td>Power OFF Time</td>
                    <td>{formatSeconds(live.poweroff_sec || 0)}</td>
                  </tr>

                  <tr>
                    <td>Planned Time</td>
                    <td>{formatSeconds(live.planned_time_sec)}</td>
                  </tr>



                </tbody>
              </table>
            </>
          )}

          {/* ===== HISTORY → OEE BREAKDOWN ===== */}
          {mode === "HISTORY" && oee && (
            <>
              <h3>CSV Data Summary</h3>

              <table>
                <thead>
                  <tr>
                    <th>Metric</th>
                    <th>Value</th>
                  </tr>
                </thead>

                <tbody>

                  <tr>
                    <td>Requested Days</td>
                    <td>{oee.requested_days ?? "-"}</td>
                  </tr>

                  <tr>
                    <td>Available CSV Days</td>
                    <td>{oee.available_days ?? "-"}</td>
                  </tr>

                  <tr>
                    <td>Missing CSV Dates</td>
                    <td>
                      {oee?.missing_days?.length > 0
                        ? oee.missing_days
                          .map(d => new Date(d).getDate().toString().padStart(2, "0"))
                          .join(", ")
                        : "None"}
                    </td>
                  </tr>

                  <tr>
                    <td>Total Runtime</td>
                    <td>{formatSeconds(oee.runtime_sec)}</td>
                  </tr>

                  <tr>
                    <td>Total Idle Time</td>
                    <td>{formatSeconds(oee.idle_sec)}</td>
                  </tr>

                  <tr>
                    <td>Total Error Time</td>
                    <td>{formatSeconds(oee.fault_sec)}</td>
                  </tr>

                  <tr>
                    <td>Total Setting Time</td>
                    <td>{formatSeconds(oee.setting_sec)}</td>
                  </tr>

                  <tr>
                    <td>Power OFF File Days</td>
                    <td>{oee.poweroff_days?.filter(d => d.file_present).length ?? "-"}</td>
                  </tr>

                  <tr>
                    <td>Power OFF Missing Files</td>
                    <td>
                      {oee.poweroff_days?.filter(d => !d.file_present).length > 0
                        ? oee.poweroff_days
                          .filter(d => !d.file_present)
                          .map(d => d.date.slice(8))
                          .join(", ")
                        : "None"}
                    </td>
                  </tr>

                  <tr>
                    <td>Total Power OFF Time</td>
                    <td>{formatSeconds(oee.total_poweroff_sec ?? 0)}</td>
                  </tr>

                </tbody>
              </table>

              {/* <div className="download-center">
                <button className="download-btn">Download Report</button>
              </div> */}
            </>
          )}

        </div>

      </div>
      </>}
    </div>
  );
}

function formatSeconds(sec = 0) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;

  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}
