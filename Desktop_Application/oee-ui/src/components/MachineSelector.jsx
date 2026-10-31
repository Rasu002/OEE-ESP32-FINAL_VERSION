import { useEffect, useState } from "react";
import { useMachine } from "../context/MachineContext";

export default function MachineSelector() {
  const { machineId, setMachineId } = useMachine();
  const [machines, setMachines] = useState([]);

  useEffect(() => {
    fetch("http://127.0.0.1:8000/api/settings/machines")
      .then(res => res.json())
      .then(data => {
        setMachines(data.filter(m => m.is_active));
        if (!machineId && data.length) {
          setMachineId(data[0].machine_id);
        }
      });
  }, []);

  return (
    <select
      value={machineId || ""}
      onChange={e => setMachineId(Number(e.target.value))}
    >
      {machines.map(m => (
        <option key={m.id} value={m.machine_id}>
          {m.machine_name} (ID {m.machine_id})
        </option>
      ))}
    </select>
  );
}
