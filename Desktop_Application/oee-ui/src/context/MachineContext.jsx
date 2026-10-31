import { createContext, useContext, useState } from "react";

const MachineContext = createContext();

export function MachineProvider({ children }) {
  const [machine, setMachine] = useState(null);
  // machine = { machine_id, machine_name }

  return (
    <MachineContext.Provider value={{ machine, setMachine }}>
      {children}
    </MachineContext.Provider>
  );
}

export function useMachine() {
  return useContext(MachineContext);
}
