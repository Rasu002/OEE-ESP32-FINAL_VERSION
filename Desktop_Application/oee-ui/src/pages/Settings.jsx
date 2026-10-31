import { useEffect, useState } from "react";
import { API_BASE as API } from "../services/api";

export default function Settings({ onBack }) {
  const [machines, setMachines] = useState([]);
  const [editing, setEditing] = useState(null);
  const [form, setForm] = useState(emptyForm());

  useEffect(loadMachines, []);

  function emptyForm() {
    return {
      machine_id: "",
      machine_name: "",
      plc_address: "",
      location: "",
      shift_start_time: "",
      shift_end_time: "",
      is_active: true,
      fault_1: "",
      fault_2: "",
      fault_3: "",
      fault_4: "",
      fault_5: ""
    };
  }


  function loadMachines() {
    fetch(`${API}/settings/machines`)
      .then(r => r.json())
      .then(setMachines);
  }

  function editMachine(m) {
    setEditing(m.id);

    setForm({
      machine_id: m.machine_id ?? "",
      machine_name: m.machine_name ?? "",
      plc_address: m.plc_address ?? "",
      location: m.location ?? "",
      shift_start_time: m.shift_start_time ?? "",
      shift_end_time: m.shift_end_time ?? "",
      fault_1: m.fault_1 ?? "",
      fault_2: m.fault_2 ?? "",
      fault_3: m.fault_3 ?? "",
      fault_4: m.fault_4 ?? "",
      fault_5: m.fault_5 ?? "",
      is_active: !!m.is_active
    });
  }
  function cancelEdit() {
    setEditing(null);
    setForm(emptyForm());
  }



  function saveMachine() {
    const method = editing ? "PUT" : "POST";
    const url = editing
      ? `${API}/settings/machine/${editing}`
      : `${API}/settings/machine`;

    fetch(url, {
      method,
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(form)
    })
      .then(async r => {
        const data = await r.json();

        if (!r.ok) {
          // 👇 Show backend validation error
          alert(data.detail || "Failed to save machine");
          return;
        }

        alert(data.message || "Saved");
        setEditing(null);
        setForm(emptyForm());
        loadMachines();
      })
      .catch(() => {
        alert("Server error. Please try again.");
      });
  }


  function deleteMachine(machine_id) {
    if (!confirm("Delete machine?")) return;

    fetch(`${API}/settings/machine/${machine_id}`, { method: "DELETE" })
      .then(r => r.json())
      .then(res => {
        alert(res.message);
        loadMachines();
      });
  }

  const FIELD_ORDER = [
    "machine_id",
    "machine_name",
    "plc_address",
    "location",
    "planned_downtime",
    "ideal_cycle_time",
    "shift_start_time",
    "shift_end_time",
    "target_count",
    "fault_1",
    "fault_2",
    "fault_3",
    "fault_4",
    "fault_5",
    "is_active"
  ];


  const PLACEHOLDERS = {
    machine_id: "Machine ID (numeric)",
    machine_name: "Machine Name",
    plc_address: "PLC COM Port (eg: COM3)",
    location: "Location / Line",
    planned_downtime: "Planned Downtime (minutes)",
    ideal_cycle_time: "Ideal Cycle Time (sec)",
    shift_start_time: "Shift Start (HH:MM)",
    shift_end_time:   "Shift End   (HH:MM)",
    target_count: "Target Production Count",
    fault_1: "Fault 1 Name",
    fault_2: "Fault 2 Name",
    fault_3: "Fault 3 Name",
    fault_4: "Fault 4 Name",
    fault_5: "Fault 5 Name"
  };


  return (
    <div className="settings-page">

      {/* HEADER */}
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 6 }}>
        <h2>⚙ Machine Settings</h2>
        <button className="back_in_s" onClick={onBack}>⬅ Back</button>
      </div>

      {/* ===== TWO COLUMN LAYOUT ===== */}
      <div className="settings-layout">

        {/* ===== LEFT : FORM ===== */}
        <div className="settings-form">

          {/* ===== MACHINE INFO ===== */}
          <div className="settings-card">
            <h4>Machine Information</h4>
            <div className="form-grid">
              <FormField label="Machine ID (Slave ID)">
                <input
                  value={form.machine_id}
                  onChange={e => setForm({ ...form, machine_id: Number(e.target.value) })}
                />
              </FormField>

              <FormField label="Machine Name">
                <input
                  value={form.machine_name}
                  onChange={e => setForm({ ...form, machine_name: e.target.value })}
                />
              </FormField>

              <FormField label="MCU IP Address">
                <input
                  value={form.plc_address}
                  onChange={e => setForm({ ...form, plc_address: e.target.value })}
                />
              </FormField>

              <FormField label="Location / Line">
                <input
                  value={form.location}
                  onChange={e => setForm({ ...form, location: e.target.value })}
                />
              </FormField>
            </div>
          </div>

          {/* ===== PRODUCTION SETTINGS ===== */}
          <div className="settings-card">
            <h4>Production Settings</h4>
            <div className="form-grid">
              {/* <FormField label="Planned Downtime (sec)">
                <input
                  type="number"
                  value={form.planned_downtime}
                  onChange={e => setForm({ ...form, planned_downtime: Number(e.target.value) })}
                />
              </FormField> */}

              {/* <FormField label="Ideal Cycle Time (sec)">
                <input
                  type="number"
                  value={form.ideal_cycle_time}
                  onChange={e => setForm({ ...form, ideal_cycle_time: Number(e.target.value) })}
                />
              </FormField> */}

              <FormField label="Shift Start Time">
                <input
                  type="time"
                  value={form.shift_start_time}
                  onChange={e => setForm({ ...form, shift_start_time: e.target.value })}
                />
              </FormField>

              <FormField label="Shift End Time">
                <input
                  type="time"
                  value={form.shift_end_time}
                  onChange={e => setForm({ ...form, shift_end_time: e.target.value })}
                />
              </FormField>

              {/* <FormField label="Target Count">
                <input
                  type="number"
                  value={form.target_count}
                  onChange={e => setForm({ ...form, target_count: Number(e.target.value) })}
                />
              </FormField> */}
            </div>
          </div>

          {/* ===== FAULTS ===== */}
          <div className="settings-card">
            <h4>Fault Names</h4>
            <div className="fault-grid">
              {["fault_1", "fault_2", "fault_3", "fault_4", "fault_5"].map(f => (
                <FormField key={f} label={f.replace("_", " ").toUpperCase()}>
                  <input
                    value={form[f]}
                    onChange={e => setForm({ ...form, [f]: e.target.value })}
                  />
                </FormField>
              ))}
            </div>
          </div>

          {/* ACTIONS */}
          <div className="settings-actions">
            <label style={{ display: "flex", alignItems: "center", gap: 6 }}>
              <input
                type="checkbox"
                checked={form.is_active}
                onChange={e => setForm({ ...form, is_active: e.target.checked })}
              />
              Active
            </label>

            <div style={{ display: "flex", gap: 10 }}>
              <button
                className="btn-secondary"
                onClick={cancelEdit}
                type="button"
              >
                ✖ Cancel
              </button>

              <button
                className="download-btn"
                onClick={saveMachine}
              >
                💾 {editing ? "Update" : "Add"} Machine
              </button>
            </div>
          </div>


        </div>

        {/* ===== RIGHT : MACHINE LIST ===== */}
        <div className="settings-list">
          <h4>Machines</h4>

          <div className="settings-table-wrap">
            <table>
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Machine</th>
                  <th>PLC</th>
                  <th>Active</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                {machines.map(m => (
                  <tr key={m.id}>
                    <td>{m.machine_id}</td>
                    <td>{m.machine_name}</td>
                    <td>{m.plc_address}</td>
                    <td>{m.is_active ? "Yes" : "No"}</td>
                    <td className="actions">
                      <button onClick={() => editMachine(m)}>✏️</button>
                      <button onClick={() => deleteMachine(m.machine_id)}>🗑</button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>

      </div>
    </div>


  );
}

function FormField({ label, children }) {
  return (
    <div className="form-field">
      <label>{label}</label>
      {children}
    </div>
  );
}

