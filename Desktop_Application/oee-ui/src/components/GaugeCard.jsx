function getGaugeColor(value) {
  if (value < 60) return "#ef4444";
  if (value < 85) return "#f59e0b";
  return "#22c55e";
}

export default function GaugeCard({
  title,
  value,
  unit = "%",
  subtitle,
  trendValue,
  trendUp = true,
  scale = 1
}) {
  const safeValue = Math.min(Math.max(Number(value) || 0, 0), 100);
  const angle = safeValue * 1.8 - 90;
  const dynamicColor = getGaugeColor(safeValue);

  return (
    <div className="gauge-card" style={{ "--gauge-scale": scale }}>
      <div className="gauge-title">{title}</div>

      <div className="gauge-wrap">
        <svg viewBox="0 0 200 120" className="gauge-svg" width="100%" height="100%" style={{ overflow: "visible" }}>
          <path
            d="M20 100 A80 80 0 0 1 180 100"
            fill="none"
            stroke="#e5e7eb"
            strokeWidth="14"
          />

          <path
            d="M20 100 A80 80 0 0 1 180 100"
            fill="none"
            stroke={dynamicColor}
            strokeWidth="14"
            strokeDasharray={`${safeValue * 2.5} 999`}
          />
        </svg>

        <div
          className="gauge-needle"
          style={{ transform: `rotate(${angle}deg)` }}
        />

        <div className="gauge-value">
          {safeValue}{unit}
        </div>
      </div>

      <div className="gauge-footer">
        <span className="trend-label"> </span>
        <span className={trendUp ? "trend-up" : "trend-down"}>
          {trendValue}
        </span>
      </div>
    </div>
  );
}
