export const API_BASE = `http://127.0.0.1:8000/api`;

// Generic helper
async function apiFetch(path, options = {}) {
  const res = await fetch(`${API_BASE}${path}`, {
    headers: { "Content-Type": "application/json" },
    ...options
  });

  if (!res.ok) {
    throw new Error(`API error ${res.status}`);
  }

  return res.json();
}

// ---------- AUTH ----------
export function login(username, password) {
  return apiFetch("/login", {
    method: "POST",
    body: JSON.stringify({ username, password })
  });
}


// ---------- LIVE ----------
export function syncRuntimeLive(machineId) {
  return apiFetch(`/sync_runtime_live?machine_id=${machineId}`);
}

// ---------- OEE ----------
export function getOEEHistory(machineId, fromDate, toDate) {
  return apiFetch(
    `/oee/history?machine_id=${machineId}&from_date=${fromDate}&to_date=${toDate}`
  );
}

// ---------- FAULTS ----------
export function getFaultSummary(machineId, fromTime, toTime) {
  let q = `/faults/summary?machine_id=${machineId}`;
  if (fromTime) q += `&from_time=${fromTime}`;
  if (toTime) q += `&to_time=${toTime}`;
  return apiFetch(q);
}

// ---------- DOWNTIME ----------
export function getDowntimeSummary(machineId, from, to) {
  let q = `/downtime/summary?machine_id=${machineId}`;
  if (from) q += `&from_time=${from}`;
  if (to) q += `&to_time=${to}`;
  return apiFetch(q);
}
