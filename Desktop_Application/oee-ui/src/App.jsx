import { useState } from "react";
import Login from "./pages/Login";
import AppLayout from "./layout/AppLayout";
import MachineSelect from "./pages/MachineSelect";
import { MachineProvider } from "./context/MachineContext";
import { useViewportScale } from "./hooks/useViewportScale";

export default function App() {
  useViewportScale();
  const [loggedIn, setLoggedIn] = useState(false);
  const [machineSelected, setMachineSelected] = useState(false);
  const [settingsMode, setSettingsMode] = useState(false);

  if (!loggedIn) {
    return <Login onLogin={() => setLoggedIn(true)} />;
  }

  return (
    <MachineProvider>
      {settingsMode ? (
        <AppLayout
          forceSettings
          onExitSettings={() => setSettingsMode(false)}
        />
      ) : !machineSelected ? (
        <MachineSelect
          onSelect={() => setMachineSelected(true)}
          onSettings={() => setSettingsMode(true)}
        />
      ) : (
        <AppLayout onChangeMachine={() => setMachineSelected(false)} />
      )}
    </MachineProvider>
  );
}
