import { useEffect, useState } from "react";
import { useMachine } from "../context/MachineContext";
import { API_BASE } from "../services/api";
import { getToday, getYesterday } from "../utils/dateUtils";
import HistoryDateFilter from "../components/HistoryDateFilter";
import { useRef } from "react";
import "./events.css";

export default function Events() {
  const { machine } = useMachine();
  const machineId = machine?.machine_id;
  const [fromTime, setFromTime] = useState("");
  const [toTime, setToTime] = useState("");
  const [summary, setSummary] = useState({});
  const [events, setEvents] = useState([]);
  const [mode, setMode] = useState("LIVE");
  const today = getToday();
  const yesterdayEnd = `${getYesterday()}T23:59`;

  useEffect(() => {
    if (!machineId) return;

    let interval;
    let isFetching = false;
    let controller;

    const fetchData = async () => {
      if (isFetching) return;
      isFetching = true;

      // Cancel previous request
      if (controller) controller.abort();
      controller = new AbortController();

      let url = `${API_BASE}/events?machine_id=${machineId}`;

      if (mode === "LIVE") {
        const from = `${today}T00:00`;
        const to = getLocalDateTime();
        url += `&from_time=${from}&to_time=${to}`;
      }

      if (mode === "HISTORY" && fromTime && toTime) {
        url += `&from_time=${fromTime}&to_time=${toTime}`;
      }

      try {
        const res = await fetch(url, {
          cache: "no-store", // 🔥 disable caching
          signal: controller.signal
        });

        const data = await res.json();

        setSummary(data.summary || {});
        setEvents(data.events || []);

      } catch (err) {
        if (err.name !== "AbortError") {
          console.error("Fetch error:", err);
        }
      } finally {
        isFetching = false;
      }
    };

    // 🚀 instant fetch
    fetchData();

    // 🔥 faster live updates
    if (mode === "LIVE") {
      interval = setInterval(fetchData, 1000); // 1 sec
    }

    return () => {
      clearInterval(interval);
      if (controller) controller.abort();
    };

  }, [machineId, mode, fromTime, toTime]);


  return (
    <div className="events-page">

      {/* ===== EVENTS PAGE ONLY STYLES ===== */}
      <style>{`
        .events-kpi-row {
          --events-kpi-scale: 1.15;
          display: grid;
          grid-template-columns: repeat(5, 1fr);
          gap: calc(10px * var(--events-kpi-scale));
          padding: 0 20px;
          margin: calc(10px * var(--events-kpi-scale)) 0 calc(14px * var(--events-kpi-scale));
        }

        .events-kpi-card {
          background: #fff;
          border-radius: calc(8px * var(--events-kpi-scale));
          padding: calc(8px * var(--events-kpi-scale)) calc(10px * var(--events-kpi-scale));
          min-height: calc(60px * var(--events-kpi-scale));
          display: flex;
          flex-direction: column;
          justify-content: center;
          box-shadow: 0 1px 3px rgba(0,0,0,0.06);
        }

        .events-kpi-title {
          font-size: calc(11px * var(--events-kpi-scale));
          color: #6b7280;
          margin-bottom: calc(2px * var(--events-kpi-scale));
          white-space: nowrap;
        }

        .events-kpi-value {
          font-size: calc(20px * var(--events-kpi-scale));
          font-weight: 700;
          color: #111827;
          line-height: 1;
        }
      `}</style>

      {/* ===== TOP BAR ===== */}
      <div className="page-top-bar">
        <div className="main-right">

          {/* DATE FILTER – always rendered */}
          <div
            className="time-filter"
          >
            <label>From</label>
            <input
              type="datetime-local"
              max={yesterdayEnd}
              value={fromTime}
              onChange={e => {
                setFromTime(e.target.value);
                setMode("HISTORY"); // 👈 auto switch
              }}
            />

            <label>To</label>
            <input
              type="datetime-local"
              max={yesterdayEnd}
              value={toTime}
              onChange={e => {
                setToTime(e.target.value);
                setMode("HISTORY"); // 👈 auto switch
              }}
            />
          </div>

          {/* LIVE BUTTON */}
          <button
            className="live-btn"
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


      {/* ===== KPI ROW ===== */}
      <div className="events-kpi-row">
        <EventsKPI title="Total Events" value={summary.total_events || 0} />
        <EventsKPI title="RUN → Idle" value={summary.run_to_stop || 0} />
        <EventsKPI title="Idle → RUN" value={summary.stop_to_run || 0} />
        <EventsKPI title="Fault Events" value={summary.fault_events || 0} />
        <EventsKPI title="MCU Offline Events" value={summary.mcu_offline_events || 0} />

      </div>

      {/* ===== TIMELINE ===== */}
      <div className="timeline-container">
        <Timeline events={events} />
      </div>

      {/* ===== BOTTOM ===== */}
      <div className="bottom-section">
        <div className="event-log">
          <h4>Event Log</h4>
          <EventTable events={events} />
        </div>

        <div className="event-summary">
          <h4>Event Summary</h4>
          <SummaryTable summary={summary} />
        </div>
      </div>

    </div>
  );
}

/* ===== EVENTS KPI COMPONENT ===== */
function EventsKPI({ title, value }) {
  return (
    <div className="events-kpi-card">
      <div className="events-kpi-title">{title}</div>
      <div className="events-kpi-value">{value}</div>
    </div>
  );
}


function Timeline({ events }) {
  if (events.length < 2) return null;

  const parseLocalTimestamp = (ts) => {
    const [date, time] = ts.split(" ");
    const [y, m, d] = date.split("-").map(Number);
    const [hh, mm, ss] = time.split(":").map(Number);
    return new Date(y, m - 1, d, hh, mm, ss);
  };

  const colorMap = {
    RUN: "#2ecc71",
    IDLE: "#f1c40f",
    FAULT: "#e74c3c",
    MCU_OFFLINE: "#95a5a6"
  };

  const ordered = [...events];

  const segments = [];
  let current = {
    state: ordered[0].new_state,
    start: parseLocalTimestamp(ordered[0].timestamp),
    end: null
  };

  for (let i = 1; i < ordered.length; i++) {
    const e = ordered[i];
    const ts = parseLocalTimestamp(e.timestamp);

    if (e.new_state !== current.state) {
      current.end = ts;
      segments.push({ ...current });
      current = {
        state: e.new_state,
        start: ts,
        end: null
      };
    }
  }


  current.end = parseLocalTimestamp(
    ordered[ordered.length - 1].timestamp
  );
  segments.push(current);

  const startTime = segments[0].start.getTime();
  const endTime = segments[segments.length - 1].end.getTime();
  const total = endTime - startTime || 1;
  const [zoom, setZoom] = useState(1);
  const [offset, setOffset] = useState(0);
  const [isPanning, setIsPanning] = useState(false);
  const startXRef = useRef(0);
  const offsetRef = useRef(0);
  // Total duration in ms
  const totalDuration = endTime - startTime;

  // How much time is visible after zoom
  const visibleDuration = totalDuration / zoom;

  // Convert pixel pan → time pan
  const pxToMs = visibleDuration / 600; // 600px ≈ viewport width

  let visibleStart = startTime - offset * pxToMs;
  let visibleEnd = visibleStart + visibleDuration;

  // Clamp to bounds
  if (visibleStart < startTime) {
    visibleStart = startTime;
    visibleEnd = startTime + visibleDuration;
  }
  if (visibleEnd > endTime) {
    visibleEnd = endTime;
    visibleStart = endTime - visibleDuration;
  }

  const handleWheel = (e) => {
    e.preventDefault();

    const delta = e.deltaY < 0 ? 0.1 : -0.1;

    setZoom(z => {
      const next = Math.min(Math.max(1, z + delta), 5); // 🔒 limit zoom
      return next;
    });
  };
  const onMouseDown = (e) => {
    setIsPanning(true);
    startXRef.current = e.clientX;
    offsetRef.current = offset;
  };

  const onMouseMove = (e) => {
    if (!isPanning) return;
    const dx = e.clientX - startXRef.current;
    setOffset(offsetRef.current + dx);
  };

  const onMouseUp = () => {
    setIsPanning(false);
  };

  const tickCount = 6;

  const ticks = Array.from({ length: tickCount }).map((_, i) => {
    const t =
      visibleStart +
      (visibleDuration / (tickCount - 1)) * i;

    const d = new Date(t);

    const isStart = i === 0;
    const isEnd = i === tickCount - 1;

    return {
      label: isStart || isEnd
        ? d.toLocaleDateString(undefined, {
          day: "2-digit",
          month: "short",
          year: "numeric"
        })
        : d.toLocaleTimeString([], {
          hour: "2-digit",
          minute: "2-digit"
        })
    };
  });

  return (
    <>
      <div
        className="timeline-viewport"
        onWheel={handleWheel}
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={onMouseUp}
        onMouseLeave={onMouseUp}
      // title="Scroll to zoom • Drag to pan • Double click to reset"
      >

        <div
          className="timeline-inner"
          style={{
            transform: `translateX(${offset}px) scaleX(${zoom})`
          }}
        >
          <div className="timeline-bar">
            {segments.map((s, i) => {
              const left = ((s.start.getTime() - startTime) / total) * 100;
              const width = ((s.end - s.start) / total) * 100;

              return (
                <div
                  key={i}
                  className="timeline-segment"
                  style={{
                    left: `${left}%`,
                    width: `${width}%`,
                    backgroundColor: colorMap[s.state]
                  }}
                  title={`${s.state}
${s.start.toLocaleTimeString()} → ${s.end.toLocaleTimeString()}`}
                />

              );
            })}
          </div>
        </div>

      </div>

      {/* ===== TIME AXIS ===== */}
      <div className="timeline-axis">
        {ticks.map((t, i) => (
          <div key={i} className="axis-label">
            {t.label}
          </div>
        ))}
      </div>

      {/* ===== LEGEND ===== */}
      <div className="timeline-legend">
        <Legend color="#2ecc71" label="RUN (Production)" />
        <Legend color="#f1c40f" label="IDLE (Idle / Planned)" />
        <Legend color="#e74c3c" label="FAULT (Breakdown)" />
        <Legend color="#95a5a6" label="MCU OFFLINE" />
      </div>

    </>
  );
}

function Legend({ color, label }) {
  return (
    <div className="legend-item">
      <span className="legend-color" style={{ backgroundColor: color }} />
      {label}
    </div>
  );
}

function EventTable({ events }) {
  const reversed = [...events].reverse();

  return (
    <div className="table-scroll">
      <table>
        <thead>
          <tr>
            <th>Time</th>
            <th>From</th>
            <th>To</th>
          </tr>
        </thead>
        <tbody>
          {reversed.map((e, i) => (
            <tr
              key={i}
              className={`row-${e.new_state?.toLowerCase()}`}
            >
              <td>{e.timestamp}</td>
              <td>{e.old_state}</td>
              <td>{e.new_state}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function SummaryTable({ summary }) {
  return (
    <div className="summary-wrapper">
      <table className="summary-table">
        <tbody>
          <tr><td>Total Events</td><td>{summary.total_events || 0}</td></tr>
          <tr><td>RUN → Idle</td><td>{summary.run_to_stop || 0}</td></tr>
          <tr><td>Idle → RUN</td><td>{summary.stop_to_run || 0}</td></tr>
          <tr><td>Fault Events</td><td>{summary.fault_events || 0}</td></tr>
        </tbody>
      </table>

      {/* DOWNLOAD BUTTON */}
      {/* <button
        className="download-btn"
        onClick={() => {
          console.log("Download report clicked (API will be added later)");
          alert("Report download API will be added later");
        }}
      >
        ⬇ Download Report
      </button> */}
    </div>
  );
}

function getLocalDateTime() {
  const d = new Date();
  d.setMinutes(d.getMinutes() - d.getTimezoneOffset());
  return d.toISOString().slice(0, 16);
}
