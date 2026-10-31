import { getYesterday } from "../utils/dateUtils";

export default function HistoryDateFilter({
  from,
  to,
  setFrom,
  setTo
}) {
  const yesterday = getYesterday();

  return (
    <div className="time-filter right">
      <label>From</label>
      <input
        type="date"
        value={from}
        max={yesterday}
        onChange={e => setFrom(e.target.value)}
      />

      <label>To</label>
      <input
        type="date"
        value={to}
        max={yesterday}
        onChange={e => setTo(e.target.value)}
      />
    </div>
  );
}
