import { useState } from "react";
import { login } from "../services/api";
import "./login.css";

export default function Login({ onLogin }) {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");

  async function handleLogin(e) {
    e.preventDefault();
    setError("");

    const res = await login(username, password);
    if (res.success) {
      onLogin();
    } else {
      setError("Invalid username or password");
    }
  }

  return (
    <div className="login-page">
      <form className="login-card" onSubmit={handleLogin}>

        <div className="login-header">
          <h2>SWAY AUTOMATION</h2>
          <p>Machine Performance Monitoring</p>
        </div>

        <div className="login-field">
          <label>Username</label>
          <input
            placeholder="Enter username"
            value={username}
            onChange={e => setUsername(e.target.value)}
            autoFocus
          />
        </div>

        <div className="login-field">
          <label>Password</label>
          <input
            type="password"
            placeholder="Enter password"
            value={password}
            onChange={e => setPassword(e.target.value)}
          />
        </div>

        {error && <div className="login-error">{error}</div>}

        <button className="login-btn" type="submit">
          🔐 Login
        </button>

        <div className="login-footer">
          © {new Date().getFullYear()} Sway Automation & Technologies
        </div>
      </form>
    </div>
  );
}
