import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  Tooltip,
  ResponsiveContainer
} from "recharts";

export default function TotalVsPlannedGraph({ live }) {
  if (!live) return null;

  const planned = live.planned_time_sec || 0;

  const total =
    (live.runtime_sec || 0) +
    (live.idle_sec || 0) +
    (live.fault_sec || 0);

  const percent = planned > 0 ? (total / planned) * 100 : 0;

  const plannedSize = 28;
  const totalSize = 28;

  const data = [
    {
      name: "Time",
      Planned: planned,
      Total: total
    }
  ];

  return (
    <div className="fault-chart-overview">

      {/* HEADER */}
      <div className="graph-header">
        <h4>Actual vs Planned Production Time</h4>
      </div>

      {/* GRAPH */}
      <div style={{ width: "100%", height: 120 }}>
        <ResponsiveContainer>
          <BarChart
            data={data}
            layout="vertical"
            barGap={-5}
            margin={{ top: -30, right: 20, left: 20, bottom: 10 }}
          >
            <XAxis type="number" hide />
            <YAxis type="category" dataKey="name" hide />

            <Tooltip
              cursor={false}
              formatter={(value) => formatSeconds(value)}
              contentStyle={{
                backgroundColor: "#ffffff",
                border: "1px solid #e5e7eb",
                borderRadius: "8px",
                fontSize: "11px",
                boxShadow: "0 4px 12px rgba(0,0,0,0.08)"
              }}
            />

            {/* 🔵 Planned */}
            <Bar
              dataKey="Planned"
              fill="#1277eb"
              barSize={plannedSize}
              radius={[5, 5, 5, 5]}
            />

            {/* 🔴 Total */}
            <Bar
              dataKey="Total"
              fill="#dc2626"
              barSize={totalSize}
              radius={[5, 5, 5, 5]}
            />

          </BarChart>
        </ResponsiveContainer>
      </div>

      {/* LEGEND */}
      <div className="graph-legend-02">
        <span className="legend-item">
          <i className="dot planned" /> Planned
        </span>

        <span className="legend-item">
          <i className="dot total" /> Total
        </span>
      </div>

    </div>
  );
}

/* ✅ FORMAT FUNCTION */
function formatSeconds(sec = 0) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);

  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}