import { useEffect, useState } from "react";
import { useMachine } from "../context/MachineContext";
import { getDowntimeSummary } from "../services/api";
import HistoryDateFilter from "../components/HistoryDateFilter";
import "./events.css";

export default function Downtime() {
  const { machine } = useMachine();
  const machineId = machine?.machine_id;

  const [mode, setMode] = useState("LIVE"); // LIVE | HISTORY
  const [fromDate, setFromDate] = useState("");
  const [toDate, setToDate] = useState("");
  const [data, setData] = useState(null);

  /* ===== DATA LOADER ===== */
  useEffect(() => {
    if (!machineId) return;

    if (mode === "LIVE") {
      getDowntimeSummary(machineId).then(setData);
    }

    if (mode === "HISTORY" && fromDate && toDate) {
      getDowntimeSummary(machineId, fromDate, toDate).then(setData);
    }
  }, [machineId, mode, fromDate, toDate]);

  if (!data) return null;

  const { kpi } = data;

  const barData = [
    { label: "Uptime", value: kpi.uptime_hrs, color: "#22c55e" },
    { label: "Stop", value: kpi.stop_hrs, color: "#f59e0b" },
    { label: "Fault", value: kpi.fault_hrs, color: "#ef4444" },
    { label: "PLC Offline", value: kpi.offline_hrs || 0, color: "#9ca3af" }
  ];

  return (
    <div className="events-page">
      <style>{`
        .events-kpi-row {
          display: grid;
          grid-template-columns: repeat(5, 1fr);
          gap: 10px;
          padding: 0 20px;
          margin: 10px 0 14px;
        }

        .events-kpi-card {
          background: #fff;
          border-radius: 8px;
          padding: 8px 10px;
          min-height: 60px;
          display: flex;
          flex-direction: column;
          justify-content: center;
          box-shadow: 0 1px 3px rgba(0,0,0,0.06);
        }

        .events-kpi-title {
          font-size: 11px;
          color: #6b7280;
          margin-bottom: 2px;
          white-space: nowrap;
        }

        .events-kpi-value {
          font-size: 20px;
          font-weight: 700;
          color: #111827;
          line-height: 1;
        }
      `}</style>

      {/* ===== TOP BAR ===== */}
      <div className="page-top-bar">

        <HistoryDateFilter
          from={fromDate}
          to={toDate}
          setFrom={(v) => {
            setFromDate(v);
            setMode("HISTORY");
          }}
          setTo={(v) => {
            setToDate(v);
            setMode("HISTORY");
          }}
        />

        <button
          onClick={() => {
            setMode("LIVE");
            setFromDate("");
            setToDate("");
          }}
        >
          Live
        </button>

      </div>


      {/* ===== KPI ROW ===== */}
      <div className="events-kpi-row">
        <DowntimeKPI title="Total Uptime" value={`${kpi.uptime_hrs} hrs`} />
        <DowntimeKPI title="Total Downtime" value={`${kpi.downtime_hrs} hrs`} />
        <DowntimeKPI title="Fault Downtime" value={`${kpi.fault_hrs} hrs`} />
        <DowntimeKPI title="PLC Offline" value={`${kpi.offline_hrs || 0} hrs`} />
        <DowntimeKPI title="Total Stops" value={kpi.total_stops} />
      </div>

      {/* ===== MAIN CONTENT ===== */}
      <div className="fault-main">

        <div className="fault-chart">
          <h4>Downtime Comparison (Hours)</h4>
          <DowntimeBarChart data={barData} />
        </div>

        <div className="fault-table-box">
          <h4>Downtime Summary</h4>

          <table>
            <thead>
              <tr>
                <th>Category</th>
                <th>Hours</th>
              </tr>
            </thead>
            <tbody>
              {barData.map(b => (
                <tr key={b.label}>
                  <td>{b.label}</td>
                  <td>{b.value}</td>
                </tr>
              ))}
            </tbody>
          </table>

          <div className="download-center">
            <button className="download-btn">
              Download Report
            </button>
          </div>
        </div>

      </div>
    </div>
  );
}

/* ===== KPI CARD ===== */
function DowntimeKPI({ title, value }) {
  return (
    <div className="downtime-kpi-card">
      <div className="downtime-kpi-title">{title}</div>
      <div className="downtime-kpi-value">{value}</div>
    </div>
  );
}

/* ===== BAR CHART ===== */
function DowntimeBarChart({ data }) {
  const max = Math.max(...data.map(d => d.value), 1);
  const CHART_HEIGHT = 240;

  return (
    <div className="cluster-chart-root">
      <div className="cluster-chart">

        <div className="y-axis">
          {data.map(d => (
            <div key={d.label} className="y-label">
              {d.label}
            </div>
          ))}
        </div>

        <div className="bars-area">
          {data.map(d => {
            const width = (d.value / max) * 100;
            const showOutside = width < 12;

            return (
              <div key={d.label} className="bar-row">
                <div
                  className="bar"
                  style={{
                    width: `${width}%`,
                    backgroundColor: d.color
                  }}
                >
                  {!showOutside && (
                    <span className="bar-value inside">
                      {d.value.toFixed(2)} h
                    </span>
                  )}
                </div>

                {showOutside && (
                  <span className="bar-value outside">
                    {d.value.toFixed(2)} h
                  </span>
                )}
              </div>
            );
          })}

        </div>
      </div>

      <div className="x-axis">
        <span>0</span>
        <span>{(max / 2).toFixed(1)}</span>
        <span>{max.toFixed(1)}</span>
      </div>

      <div className="x-axis-title">Hours</div>
    </div>
  );
}
