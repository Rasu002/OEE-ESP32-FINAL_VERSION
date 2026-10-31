import {
    BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell
} from "recharts";

function formatSeconds(sec = 0) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return `${h}h ${m}m`;
}

const CustomTooltip = ({ active, payload }) => {
    if (active && payload && payload.length) {
        const item = payload[0].payload;
        return (
            <div style={{
                backgroundColor: "#ffffff",
                border: "1px solid #e5e7eb",
                borderRadius: "8px",
                padding: "8px 12px",
                fontSize: "12px",
                boxShadow: "0 4px 12px rgba(0,0,0,0.08)"
            }}>
                <span style={{ fontWeight: 600, color: item.color }}>{item.name}</span>
                <br />
                <span style={{ color: "#374151" }}>{formatSeconds(item.value)}</span>
            </div>
        );
    }
    return null;
};

export default function HistoryMachineGraph({ oee }) {
    if (!oee) return null;

    const totalSec = (oee.runtime_sec || 0) + (oee.idle_sec || 0) + (oee.fault_sec || 0);

    const data = [
        { name: "Run",   value: oee.runtime_sec || 0, color: "#16a34a" },
        { name: "Idle",  value: oee.idle_sec    || 0, color: "#f59e0b" },
        { name: "Error", value: oee.fault_sec   || 0, color: "#e43b29" },
        { name: "Setting", value: oee.setting_sec   || 0, color: "#73271f" },
    ];

    return (
        <div className="fault-chart-overview">
            <div className="graph-header">
                <h4>Time Status (History)</h4>
                <span className="graph-subtitle">
                    Total: {formatSeconds(totalSec)} • Days: {oee.available_days || 0}
                </span>
            </div>

            <div style={{ width: "100%", height: 120 }}>
                <ResponsiveContainer>
                    <BarChart
                        data={data}
                        layout="vertical"
                        barGap={-28}
                        margin={{ top: 0, right: 20, left: 20, bottom: 0 }}
                    >
                        <XAxis type="number" hide />
                        <YAxis type="category" dataKey="name" hide />
                        <Tooltip
                            content={<CustomTooltip />}
                            cursor={false}
                        />
                        <Bar dataKey="value" radius={[5, 5, 5, 5]}>
                            {data.map((entry, index) => (
                                <Cell key={index} fill={entry.color} />
                            ))}
                        </Bar>
                    </BarChart>
                </ResponsiveContainer>
            </div>

            <div className="graph-legend">
                {data.map(item => (
                    <span key={item.name}>
                        <i style={{
                            background: item.color,
                            borderRadius: "50%",
                            display: "inline-block",
                            width: 8, height: 8, marginRight: 4
                        }} />
                        {item.name}
                    </span>
                ))}
            </div>
        </div>
    );
}