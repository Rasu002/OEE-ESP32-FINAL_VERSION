import { useEffect, useState } from "react";
import { useMachine } from "../context/MachineContext";
import { getFaultSummary } from "../services/api";
import { getYesterday } from "../utils/dateUtils";
import { getToday } from "../utils/dateUtils";
import "./events.css";

export default function Faults() {
  const { machine } = useMachine();
  const machineId = machine?.machine_id;
  const [fromTime, setFromTime] = useState("");
  const [toTime, setToTime] = useState("");
  const [data, setData] = useState(null);
  const [mode, setMode] = useState("LIVE");
  const yesterdayEnd = `${getYesterday()}T23:59`;
  const today = getToday();

  useEffect(() => {
  if (!machineId) return;

  const fetchData = () => {
    if (mode === "LIVE") {
      const from = `${today}T00:00`;
      getFaultSummary(machineId, from).then(setData);
    }

    if (mode === "HISTORY" && fromTime && toTime) {
      getFaultSummary(machineId, fromTime, toTime).then(setData);
    }
  };

  fetchData();

  const interval = setInterval(fetchData, 1000);

  return () => clearInterval(interval);

}, [machineId, mode, fromTime, toTime]);



  if (!data) return null;

  const { kpi, pareto, faults } = data;
  const topFault = pareto?.[0]; // already sorted by total_hours
  const totalFaults = pareto.reduce((sum, f) => sum + f.count, 0);

  const topFaultPercent = topFault
    ? Math.round((topFault.count / totalFaults) * 100)
    : 0;

  return (
    <div className="events-page">

      {/* ===== TOP BAR ===== */}
      <div className="page-top-bar">
        <div className="main-right">

          {/* DATE FILTER – always clickable */}
          <div className="time-filter">
            <label>From</label>
            <input
              type="datetime-local"
              max={yesterdayEnd}   // ✅ hides today + future
              value={fromTime}
              onChange={e => {
                setFromTime(e.target.value);
                setMode("HISTORY");
              }}
            />

            <label>To</label>
            <input
              type="datetime-local"
              max={yesterdayEnd}   // ✅ hides today + future
              value={toTime}
              onChange={e => {
                setToTime(e.target.value);
                setMode("HISTORY");
              }}
            />
          </div>


          {/* LIVE BUTTON */}
          <button
            className={`live-btn ${mode === "LIVE" ? "active" : ""}`}
            onClick={() => {
              setMode("LIVE");
              setFromTime("");
              setToTime("");
            }}
          >
            Live
          </button>


        </div>
      </div>
      {/* ===== KPI ===== */}
      <div className="kpi-row">
        <KPI title="Total Fault Time" value={formatDuration(kpi.total_fault_hours * 3600)} />
        <KPI title="Fault Count" value={kpi.fault_count} />
        <KPI title="MTBF" value={formatDuration(kpi.mtbf_hours * 3600)} />
        <KPI
          title="Longest Fault"
          value={
            kpi.longest_fault
              ? `${kpi.longest_fault.fault_name} (${formatDuration(kpi.longest_fault.duration_min * 60)})`
              : "-"
          }
        />

      </div>

      {/* ===== MAIN CONTENT ===== */}
      <div className="fault-main">

        {/* LEFT : BAR CHART + PARETO TABLE stacked */}
        <div className="fault-left-col">
          <div className="fault-chart">
            <h4>Fault Duration Analysis</h4>
            <ParetoBarChart data={pareto} />
          </div>
          <div className="fault-table-box">
            <h4>Fault Summary</h4>
            <FaultTable data={pareto} />
          </div>
        </div>

        {/* RIGHT : FAULT EVENT LOG full height */}
        <div className="fault-table-box">
          <h4>Fault Event Log</h4>
          <FaultEventTable data={faults} />
        </div>

      </div>
    </div>
  );
}

/* ================= COMPONENTS ================= */

function KPI({ title, value }) {
  return (
    <div className="kpi-card">
      <div className="kpi-title">{title}</div>
      <div className="kpi-value">{value}</div>
    </div>
  );
}

function ParetoBarChart({ data }) {
  if (!data || data.length === 0) {
    return <div>No fault data</div>;
  }

  const count = data.length;

  // Chart width you want to occupy (px)
  const CHART_USABLE_WIDTH = 350;

  // Bar width = total width / number of bars
  const BAR_WIDTH = Math.min(
    80,
    Math.floor(CHART_USABLE_WIDTH / count)
  );

  const GAP = 20;


  const CHART_HEIGHT = 150;
  const max = Math.max(...data.map(d => d.total_sec));



  // 🔥 Dynamic sizing

  return (
    <div style={{ padding: "20px 20px 10px 20px" }}>

      {/* ===== CHART AREA ===== */}
      <div style={{ display: "flex", justifyContent: "center" }}>

        {/* Y AXIS */}
        <div
          style={{
            width: 50,
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
            alignItems: "flex-end",
            paddingRight: 6,
            fontSize: 12,
            color: "#444"
          }}
        >
          <div>{formatDuration(max)}</div>
          <div>{formatDuration(max / 2)}</div>
          <div>0</div>
        </div>

        {/* BARS + X AXIS */}
        <div style={{ flex: 1 }}>

          {/* BARS */}
          <div
            style={{
              height: CHART_HEIGHT,
              display: "flex",
              alignItems: "flex-end",
              justifyContent: count <= 5 ? "center" : "flex-start",
              gap: GAP,
              borderLeft: "1px solid #999",
              borderBottom: "1px solid #999",
              padding: "0 30px"
            }}
          >
            {data.map(f => {
              const barHeight =
                max === 0 ? 0 : (f.total_sec / max) * CHART_HEIGHT;

              return (
                <div
                  key={f.fault_code}
                  style={{
                    width: BAR_WIDTH,
                    display: "flex",
                    flexDirection: "column",
                    alignItems: "center",
                    justifyContent: "flex-end"
                  }}
                >
                  {/* VALUE */}
                  <div
                    style={{
                      fontSize: 12,
                      marginBottom: 6,
                      fontWeight: 600
                    }}
                  >
                    {formatDuration(f.total_sec)}
                  </div>

                  {/* BAR */}
                  <div
                    style={{
                      width: "100%",
                      height: barHeight,
                      background: "#3b82f6",
                      borderRadius: "6px 6px 0 0"
                    }}
                  />
                </div>
              );
            })}
          </div>

          {/* X AXIS LABELS */}
          <div
            style={{
              display: "flex",
              justifyContent: count <= 5 ? "center" : "flex-start",
              gap: GAP,
              padding: "8px 30px 0 30px"
            }}
          >
            {data.map(f => (
              <div
                key={f.fault_code}
                style={{
                  width: BAR_WIDTH,
                  textAlign: "center",
                  fontSize: 12,
                  lineHeight: "1.2",
                  wordBreak: "break-word"
                }}
                title={f.fault_name}
              >
                {f.fault_name}
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* X AXIS TITLE */}
      <div
        style={{
          fontSize: 14,
          color: "#555",
          marginTop: 10,
          textAlign: "center"
        }}
      >
        Fault Name
      </div>
    </div>
  );
}

function FaultEventTable({ data }) {
  if (!data || data.length === 0)
    return <div style={{ padding: 8, color: "#6b7280" }}>No fault events</div>;

  const sorted = [...data].sort(
    (a, b) => new Date(b.start_time) - new Date(a.start_time)
  );

  return (
    <div className="table-scroll">
      <table>
        <thead>
          <tr>
            <th>#</th>
            <th>Fault Name</th>
            <th>Start Time</th>
            <th>End Time</th>
            <th>Duration</th>
          </tr>
        </thead>
        <tbody>
          {sorted.map((f, i) => (
            <tr key={i}>
              <td>{sorted.length - i}</td>
              <td>{f.fault_name}</td>
              <td>{f.start_time}</td>
              <td>
                {f.end_time
                  ? f.end_time
                  : <span style={{ color: "#ef4444", fontWeight: 600 }}>Active ●</span>}
              </td>
              <td>{formatDuration(f.duration_sec)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function FaultTable({ data }) {
  return (
    <div className="table-scroll">
      <table>
        <thead>
          <tr>
            <th>Fault</th>
            <th>Count</th>
            <th>Total</th>
            <th>Avg</th>
          </tr>
        </thead>
        <tbody>
          {data.map(f => (
            <tr key={f.fault_code}>
              <td>{f.fault_name}</td>
              <td>{f.count}</td>
              <td>{formatDuration(f.total_sec)}</td>
              <td>{formatDuration(f.avg_sec)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function formatDuration(sec = 0) {
  const s = Math.round(sec);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${r}s`;
  return `${r}s`;
}
