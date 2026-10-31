import { useState, useEffect } from "react";
import { CapacitorHttp } from "@capacitor/core";

const API_BASE = "http://10.219.90.15";

export default function RelayControl() {
  const [relays, setRelays] = useState({ 1: false, 2: false });
  const [loading, setLoading] = useState(false);
  const [message, setMessage] = useState("");

  async function fetchStatus() {
    try {
      const res = await CapacitorHttp.get({ url: `${API_BASE}/status` });
      setRelays({
        1: res.data.relay1,
        2: res.data.relay2,
      });
      setMessage("");
    } catch {
      setMessage("ESP32 offline");
    }
  }

  useEffect(() => {
    fetchStatus();
    const t = setInterval(fetchStatus, 3000);
    return () => clearInterval(t);
  }, []);

  async function toggleRelay(relay) {
    const next = !relays[relay];
    setRelays((p) => ({ ...p, [relay]: next }));
    setLoading(true);

    try {
      await CapacitorHttp.get({
        url: `${API_BASE}/relay`,
        params: { [`relay${relay}`]: next ? "ON" : "OFF" },
      });
    } catch {
      setRelays((p) => ({ ...p, [relay]: !next }));
      setMessage("Command failed");
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="app">
    <div className="content">
      <header className="header">
        <h1>Relay Controller</h1>
        <p>ESP32 • TCA9554</p>
      </header>

      <main className="grid">
        <RelayCard
          title="Relay 1"
          desc="Main Load"
          on={relays[1]}
          onToggle={() => toggleRelay(1)}
          disabled={loading}
        />
        <RelayCard
          title="Relay 2"
          desc="Auxiliary Load"
          on={relays[2]}
          onToggle={() => toggleRelay(2)}
          disabled={loading}
        />
      </main>

      {loading && <div className="status">Updating…</div>}
      {message && <div className="status error">{message}</div>}
      </div>
    </div>
  );
}

/* ---------- Card ---------- */
function RelayCard({ title, desc, on, onToggle, disabled }) {
  return (
    <div className="card">
      <div className="card-text">
        <h2>{title}</h2>
        <span>{desc}</span>
      </div>

      <button
        className={`toggle ${on ? "on" : "off"}`}
        disabled={disabled}
        onClick={onToggle}
      >
        {on ? "ON" : "OFF"}
      </button>
    </div>
  );
}
