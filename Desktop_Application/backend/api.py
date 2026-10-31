# api.py
from fastapi import FastAPI, Query
import sqlite3
from contextlib import contextmanager
from fastapi.middleware.cors import CORSMiddleware
from fastapi import Body
from datetime import datetime, date
from typing import Optional
from collections import defaultdict
from fastapi import HTTPException
import os, sys
import threading
from fastapi import Request
import requests
from datetime import datetime, timedelta
import csv
import time

try:
    import mcu_run as mcu
except ModuleNotFoundError:
    from server.backend import mcu_run as mcu  # type: ignore

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

SHUTDOWN_EVENT = threading.Event()

def app_path(filename):
    if hasattr(sys, "_MEIPASS"):
        # Packaged (PyInstaller / Tauri sidecar) — write user data to AppData
        if os.name == "nt":
            base = os.environ.get("APPDATA", os.path.expanduser("~"))
        else:
            base = os.path.expanduser("~/.local/share")
        data_dir = os.path.join(base, "OEEApp")
        os.makedirs(data_dir, exist_ok=True)
        return os.path.join(data_dir, filename)
    return os.path.join(os.path.dirname(__file__), filename)

DB_PATH    = app_path("oee.db")
SETTINGS_DB = DB_PATH

# CSV upload directory — must use app_path() so it survives app restarts.
# In dev:          backend/csv_uploads/
# In Tauri build:  %APPDATA%\OEEApp\csv_uploads\  (persistent, not temp)
UPLOAD_DIR = app_path("csv_uploads")
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = FastAPI(title="OEE Read API")

# Throttle: poweroff sync runs at most once per 60 s per machine (called inside
# sync_runtime_live which is polled every few seconds by the frontend).
_poweroff_last_sync: dict[int, float] = {}
_POWEROFF_SYNC_INTERVAL = 60          # seconds

# In-memory cache of last-good ESP32 runtime values per machine.
# Used as offline fallback — no DB write means no SQLite write-lock contention
# blocking concurrent ESP32 → backend push requests (state changes, CSV uploads).
_runtime_cache: dict[int, dict] = {}  # {machine_id: {runtime_sec, idle_sec, fault_sec}}

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@contextmanager
def get_settings_db():
    conn = sqlite3.connect(SETTINGS_DB)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()
@contextmanager
def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()          # <-- critical line
    finally:
        conn.close()

def init_settings_db():
    with get_settings_db() as conn:

        # TABLE 1 : MACHINE MASTER 
        conn.execute("""
        CREATE TABLE IF NOT EXISTS machine_master (
            machine_id INTEGER PRIMARY KEY,
            machine_name TEXT,
            ip_address TEXT,
            location TEXT,
            planned_downtime INTEGER,
            is_active INTEGER DEFAULT 1,
            fault_1 TEXT,
            fault_2 TEXT,
            fault_3 TEXT,
            fault_4 TEXT,
            fault_5 TEXT,
            target_count INTEGER,
            ideal_cycle_time INTEGER,
            shift_start_time TEXT,
            shift_end_time   TEXT
        );
        """)

        # Backward-compatible migration: existing DBs may still have the old
        # runtime_of_machine column and be missing the new shift columns.
        try:
            mm_cols = {r[1] for r in conn.execute("PRAGMA table_info(machine_master)").fetchall()}
            if "shift_start_time" not in mm_cols:
                conn.execute("ALTER TABLE machine_master ADD COLUMN shift_start_time TEXT")
            if "shift_end_time" not in mm_cols:
                conn.execute("ALTER TABLE machine_master ADD COLUMN shift_end_time TEXT")
        except Exception:
            pass

        conn.execute("""
        CREATE TABLE IF NOT EXISTS poweroff_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            machine_id INTEGER,
            from_time TEXT,
            to_time TEXT,
            duration_sec INTEGER,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
        """)

        # Tracks which M01PMMDD.CSV files were received from ESP32.
        # One row per machine per date — used in history to flag missing files.
        conn.execute("""
        CREATE TABLE IF NOT EXISTS poweroff_file_log (
            machine_id INTEGER,
            date       TEXT,
            filename   TEXT,
            received_at TEXT DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (machine_id, date)
        )
        """)


def table_exists(conn: sqlite3.Connection, table_name: str) -> bool:
    row = conn.execute(
        """
        SELECT 1
        FROM sqlite_master
        WHERE type = 'table' AND name = ?
        LIMIT 1
        """,
        (table_name,),
    ).fetchone()
    return row is not None

def clean_text(value):
    if value is None:
        return None
    value = str(value).strip()
    return value or None

def parse_machine_payload(payload: dict, fallback_machine_id: Optional[int] = None):
    raw_machine_id = payload.get("machine_id", fallback_machine_id)

    if raw_machine_id in (None, ""):
        raise HTTPException(status_code=400, detail="Machine ID is required.")

    try:
        machine_id = int(raw_machine_id)
    except (TypeError, ValueError):
        raise HTTPException(status_code=400, detail="Machine ID must be numeric.")

    if machine_id <= 0:
        raise HTTPException(status_code=400, detail="Machine ID must be greater than 0.")

    machine_name = clean_text(payload.get("machine_name"))
    if not machine_name:
        raise HTTPException(status_code=400, detail="Machine name is required.")

    ip_address = clean_text(payload.get("ip_address") or payload.get("plc_address"))
    if not ip_address:
        raise HTTPException(status_code=400, detail="Machine IP address is required.")

    return machine_id, machine_name, ip_address

def extract_machine_state(data: dict | None) -> Optional[str]:
    if not isinstance(data, dict):
        return None

    for key in ("machine_state", "state", "status", "new_state", "relay_state"):
        raw_value = data.get(key)
        if not isinstance(raw_value, str):
            continue

        state = raw_value.strip().upper()
        if state in {"RUN", "RUNNING"}:
            return "RUN"
        if state == "IDLE":
            return "IDLE"
        if state == "FAULT":
            return "FAULT"
        if state in {"STOP", "STOPPED"}:
            return "STOP"
        if state in {"MCU_OFFLINE", "OFFLINE"}:
            return "MCU_OFFLINE"

    return None

def resolve_machine_state(last_event_state: Optional[str], direct_state: Optional[str], plc_status: str) -> str:
    if plc_status == "OFFLINE":
        return "MCU_OFFLINE"
    if direct_state:
        return direct_state
    if last_event_state and last_event_state != "MCU_OFFLINE":
        return last_event_state
    return "UNKNOWN"

def _calc_shift_seconds(shift_start: str | None, shift_end: str | None) -> int:
    """Return shift duration in seconds from two HH:MM strings.
    Handles overnight shifts (e.g. 22:00 → 06:00).
    Returns 0 if either value is missing or unparseable.
    """
    if not shift_start or not shift_end:
        return 0
    try:
        from datetime import timedelta
        fmt = "%H:%M"
        s = datetime.strptime(shift_start[:5], fmt)
        e = datetime.strptime(shift_end[:5],   fmt)
        if e <= s:                       # overnight shift
            e += timedelta(days=1)
        return int((e - s).total_seconds())
    except Exception:
        return 0


def _poweroff_in_shift(from_str: str, to_str: str,
                       shift_start: str | None, shift_end: str | None) -> int:
    """Seconds of one power-off event that overlap the shift window.
    Returns 0 for events fully outside the shift.
    """
    if not shift_start or not shift_end:
        return 0
    try:
        fmt_dt = "%Y-%m-%d %H:%M:%S"
        fmt_t  = "%H:%M"
        ev_from = datetime.strptime(from_str, fmt_dt)
        ev_to   = datetime.strptime(to_str,   fmt_dt)
        date    = ev_from.date()
        s = datetime.combine(date, datetime.strptime(shift_start[:5], fmt_t).time())
        e = datetime.combine(date, datetime.strptime(shift_end[:5],   fmt_t).time())
        if e <= s:
            e += timedelta(days=1)
        overlap_start = max(ev_from, s)
        overlap_end   = min(ev_to,   e)
        if overlap_end <= overlap_start:
            return 0
        return int((overlap_end - overlap_start).total_seconds())
    except Exception:
        return 0


def _read_poweroff_csv_for_date(machine_id: int, date_str: str,
                                shift_start: str | None,
                                shift_end: str | None) -> tuple[bool, int]:
    """Read M{mid}P{MMDD}.CSV and return (file_present, shift_clipped_seconds).
    Events outside the shift window contribute 0 seconds.
    """
    try:
        dt    = datetime.strptime(date_str, "%Y-%m-%d")
        fname = f"M{machine_id:02d}P{dt.strftime('%m%d')}.CSV"
        path  = os.path.join(UPLOAD_DIR, fname)
        if not os.path.exists(path):
            return False, 0
        total = 0
        with open(path, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                from_str = (row.get("from_time") or "").strip()
                to_str   = (row.get("to_time")   or "").strip()
                if from_str and to_str:
                    total += _poweroff_in_shift(from_str, to_str, shift_start, shift_end)
        return True, total
    except Exception:
        return False, 0


def normalize_time(t: str) -> str:
    # Converts:
    # 2026-01-01T06:05 → 2026-01-01 06:05:00
    if "T" in t and len(t) == 16:
        return t.replace("T", " ") + ":00"
    return t

@app.on_event("startup")
def startup_event():
    mcu.init_db()
    init_settings_db()


     # 🚀 START BACKGROUND THREAD
    thread = threading.Thread(target=update_machine_status, daemon=True)
    thread.start()

@app.post("/api/login")
def login(payload: dict = Body(...)):
    username = payload.get("username")
    password = payload.get("password")

    if username == "admin" and password == "admin123":
        return {"success": True, "user": "admin"}
    return {"success": False, "message": "Invalid credentials"}

@app.get("/api/downtime/summary")
def downtime_summary(
    machine_id: int,
    from_time: str = Query(None),
    to_time: str = Query(None)
):
    now = datetime.now()

    def normalize_time(t: str, end=False) -> str:
        if len(t) == 10:  # YYYY-MM-DD
            return t + (" 23:59:59" if end else " 00:00:00")
        if "T" in t and len(t) == 16:  # datetime-local
            return t.replace("T", " ") + ":00"
        return t

    # ===== LIVE MODE (TODAY ONLY) =====
    if not from_time and not to_time:
        from_dt = datetime.combine(date.today(), datetime.min.time())
        to_dt = now
    else:
        from_dt = datetime.strptime(normalize_time(from_time, False), "%Y-%m-%d %H:%M:%S")
        to_dt = datetime.strptime(normalize_time(to_time, True), "%Y-%m-%d %H:%M:%S")

    sql = """
        SELECT timestamp, new_state
        FROM event_log
        WHERE machine_id = ?
          AND timestamp >= ?
          AND timestamp <= ?
        ORDER BY timestamp ASC
    """

    with get_db() as conn:
        rows = conn.execute(
            sql,
            [machine_id, from_dt, to_dt]
        ).fetchall()

    if not rows:
        return {
            "kpi": {
                "uptime_hrs": 0,
                "downtime_hrs": 0,
                "stop_hrs": 0,
                "fault_hrs": 0,
                "offline_hrs": 0,
                "total_stops": 0
            },
            "chart": []
        }

    # ===== TIME CALCULATION =====
    uptime = fault = stop = offline = 0
    stop_count = 0

    def ts(x):
        return datetime.strptime(x, "%Y-%m-%d %H:%M:%S")

    for i in range(len(rows)):
        curr = rows[i]
        start = ts(curr["timestamp"])

        if i + 1 < len(rows):
            end = ts(rows[i + 1]["timestamp"])
        else:
            end = min(now, to_dt)   # 👈 COUNT LAST STATE UNTIL NOW

        duration = max((end - start).total_seconds(), 0)
        state = curr["new_state"]

        if state == "RUN":
            uptime += duration
        elif state == "FAULT":
            fault += duration
        elif state == "IDLE":
            stop += duration
            stop_count += 1
        elif state == "MCU_OFFLINE":
            offline += duration

    # ===== KPI =====
    uptime_hrs = round(uptime / 3600, 2)
    fault_hrs = round(fault / 3600, 2)
    stop_hrs = round(stop / 3600, 2)
    offline_hrs = round(offline / 3600, 2)

    downtime_hrs = round(fault_hrs + stop_hrs + offline_hrs, 2)

    kpi = {
        "uptime_hrs": uptime_hrs,
        "downtime_hrs": downtime_hrs,
        "stop_hrs": stop_hrs,
        "fault_hrs": fault_hrs,
        "offline_hrs": offline_hrs,
        "total_stops": stop_count
    }

    chart = [
        {"category": "Stop", "hours": stop_hrs},
        {"category": "Fault", "hours": fault_hrs},
        {"category": "PLC Offline", "hours": offline_hrs}
    ]

    return {"kpi": kpi, "chart": chart}

@app.get("/api/events")
def event_log(
    machine_id: int,
    from_time: Optional[str] = Query(None),
    to_time: Optional[str] = Query(None)
):
    """
    from_time, to_time expected from UI as:
    YYYY-MM-DDTHH:MM   (datetime-local)
    Stored DB format:
    YYYY-MM-DD HH:MM:SS
    """

    base_sql = """
        FROM event_log
        WHERE machine_id = ?
    """
    params = [machine_id]

    #  Normalize datetime-local → SQLite format
    def normalize_time(t: str, is_end=False) -> str:
    # If only DATE is sent → expand to full day
        if len(t) == 10:  # YYYY-MM-DD
            if is_end:
                return t + " 23:59:59"
            return t + " 00:00:00"

        # If datetime-local
        if "T" in t and len(t) == 16:
            return t.replace("T", " ") + ":00"

        return t

    if from_time:
        base_sql += " AND timestamp >= ?"
        params.append(normalize_time(from_time))

    if to_time:
        base_sql += " AND timestamp < ?"
        params.append(normalize_time(to_time)[:-3] + ":59")

    with get_db() as conn:

        #  EVENT LIST
        events = conn.execute(f"""
            SELECT
                timestamp,
                old_state,
                new_state
            {base_sql}
            ORDER BY timestamp ASC
        """, params).fetchall()

        # SUMMARY COUNTS 
        summary = conn.execute(f"""
            SELECT
                COUNT(*) AS total_events,

                SUM(
                    CASE WHEN old_state = 'RUN' AND new_state = 'IDLE'
                    THEN 1 ELSE 0 END
                ) AS run_to_stop,

                SUM(
                    CASE WHEN old_state = 'IDLE' AND new_state = 'RUN'
                    THEN 1 ELSE 0 END
                ) AS stop_to_run,

                SUM(
                    CASE WHEN new_state = 'FAULT'
                    THEN 1 ELSE 0 END
                ) AS fault_events,

                SUM(
                    CASE WHEN new_state = 'MCU_OFFLINE'
                    THEN 1 ELSE 0 END
                ) AS mcu_offline_events

            {base_sql}
        """, params).fetchone()

    return {
        "summary": dict(summary),
        "events": [dict(e) for e in events]
    }


@app.get("/api/faults/summary")
def fault_summary(
    machine_id: int,
    from_time: str = Query(None),
    to_time: str = Query(None)
):
    # ==============================
    # BASE QUERY
    # ==============================
    sql = """
        SELECT start_time, end_time, duration_sec, fault_code
        FROM fault_log
        WHERE machine_id = ?
    """
    params = [machine_id]

    # ==============================
    # NORMALIZE TIME INPUT
    # ==============================
    if from_time and not to_time:
        day = normalize_time(from_time)[:10]
        from_time = f"{day} 00:00:00"
        to_time = f"{day} 23:59:59"

    if from_time:
        sql += " AND start_time >= ?"
        params.append(normalize_time(from_time))

    if to_time:
        sql += " AND start_time <= ?"
        params.append(normalize_time(to_time))

    # ==============================
    # FETCH DATA
    # ==============================
    with get_db() as conn:
        rows = conn.execute(sql, params).fetchall()

        run_sql = "SELECT SUM(runtime_seconds) AS run_sec FROM runtime_daily WHERE machine_id = ?"
        run_params = [machine_id]
        if from_time:
            run_params.append(normalize_time(from_time)[:10])
            run_sql += " AND date >= ?"
        if to_time:
            run_params.append(normalize_time(to_time)[:10])
            run_sql += " AND date <= ?"
        run_row = conn.execute(run_sql, run_params).fetchone()

    faults = [dict(r) for r in rows]

    # ==============================
    # HANDLE EMPTY
    # ==============================
    if not faults:
        return {
            "kpi": {},
            "pareto": [],
            "table": [],
            "faults": []
        }

    # ==============================
    # LIVE FAULT DURATION FIX
    # ==============================
    now = datetime.now()

    for f in faults:
        if f["duration_sec"] is None:
            try:
                start = datetime.fromisoformat(f["start_time"])
            except:
                start = datetime.strptime(f["start_time"], "%Y-%m-%d %H:%M:%S")

            f["duration_sec"] = (now - start).total_seconds()

    # ==============================
    # FAULT NAME MAP
    # ==============================
    with get_settings_db() as sdb:
        row = sdb.execute("""
            SELECT fault_1, fault_2, fault_3, fault_4, fault_5
            FROM machine_master
            WHERE machine_id = ?
        """, (machine_id,)).fetchone()

    fault_name_map = {}
    if row:
        fault_name_map = {
            1: row["fault_1"],
            2: row["fault_2"],
            3: row["fault_3"],
            4: row["fault_4"],
            5: row["fault_5"]
        }

    # Add fault_name to each raw fault event for the event log table
    for f in faults:
        code = int(f["fault_code"]) if f["fault_code"] is not None else 0
        name = fault_name_map.get(code)
        f["fault_name"] = name if name else f"Fault {code}"

    # ==============================
    # KPI CALCULATIONS
    # ==============================
    total_fault_sec = sum(f["duration_sec"] for f in faults)
    fault_count = len(faults)
    total_run_sec = run_row["run_sec"] or 0

    # If the date range includes today and runtime_daily has no entry yet
    # (CSV not uploaded until next boot), supplement with live cache
    today_str = date.today().isoformat()
    range_includes_today = (
        (not from_time or normalize_time(from_time)[:10] <= today_str) and
        (not to_time   or normalize_time(to_time)[:10]   >= today_str)
    )
    if range_includes_today:
        cached = _runtime_cache.get(machine_id)
        if cached:
            total_run_sec += cached["runtime_sec"]

    mtbf_hours = round(
        (total_run_sec / fault_count) / 3600, 2
    ) if fault_count > 0 else 0

    mttr_hours = round(
        (total_fault_sec / fault_count) / 3600, 2
    ) if fault_count > 0 else 0

    longest_fault = max(faults, key=lambda x: x["duration_sec"])

    kpi = {
        "total_fault_hours": round(total_fault_sec / 3600, 2),
        "fault_count": fault_count,
        "mtbf_hours": mtbf_hours,
        "mttr_hours": mttr_hours,
        "longest_fault": {
            "fault_code": longest_fault["fault_code"],
            "fault_name": fault_name_map.get(longest_fault["fault_code"]) or str(longest_fault["fault_code"]),
            "duration_min": round(longest_fault["duration_sec"] / 60, 1)
        }
    }

    # ==============================
    # PARETO
    # ==============================
    agg = defaultdict(lambda: {
        "fault_code": None,
        "fault_name": None,
        "count": 0,
        "total_sec": 0
    })

    for f in faults:
        code = f["fault_code"]
        agg[code]["fault_code"] = code
        int_code = int(code) if code is not None else 0
        n = fault_name_map.get(int_code)
        agg[code]["fault_name"] = n if n else f"Fault {code}"
        agg[code]["count"] += 1
        agg[code]["total_sec"] += f["duration_sec"]

    pareto = sorted(
        [
            {
                "fault_code": k,
                "fault_name": v["fault_name"],
                "count": v["count"],
                "total_hours": round(v["total_sec"] / 3600, 2),
                "total_sec": v["total_sec"],
                "avg_min": round((v["total_sec"] / v["count"]) / 60, 1),
                "avg_sec": int(v["total_sec"] / v["count"])
            }
            for k, v in agg.items()
        ],
        key=lambda x: x["total_hours"],
        reverse=True
    )

    # ==============================
    # FINAL RESPONSE
    # ==============================
    return {
        "kpi": kpi,
        "pareto": pareto,
        "table": pareto,
        "faults": faults
    }

def check_device(machine_id: int, ip: Optional[str]) -> tuple[str, dict | None]:
    if not ip:
        return "NOT_CONFIGURED", None

    try:
        connected, data = mcu.check_ip_connection(machine_id, ip)
        return ("ONLINE" if connected else "OFFLINE"), data
    except Exception:
        return "OFFLINE", None

@app.put("/api/settings/machine/{row_id}")
def update_machine(row_id: int, payload: dict = Body(...)):

    old_machine_id = int(row_id)
    machine_id, machine_name, ip_address = parse_machine_payload(payload, old_machine_id)
    is_active = 1 if payload.get("is_active", True) else 0

    with get_settings_db() as conn:

        if machine_id != old_machine_id:
            exists = conn.execute(
                "SELECT 1 FROM machine_master WHERE machine_id = ?",
                (machine_id,)
            ).fetchone()
            if exists:
                raise HTTPException(
                    status_code=400,
                    detail="Machine ID already exists. Please use a different Machine ID."
                )

            for table in ("machine_daily_cumulative", "event_log", "fault_log", "runtime_live", "runtime_daily"):
                if table_exists(conn, table):
                    conn.execute(
                        f"UPDATE {table} SET machine_id = ? WHERE machine_id = ?",
                        (machine_id, old_machine_id)
                    )

        # UPDATE MASTER
        conn.execute("""
            UPDATE machine_master SET
                machine_id = ?,
                machine_name = ?,
                ip_address = ?,
                location = ?,
                shift_start_time = ?,
                shift_end_time   = ?,
                is_active = ?,
                fault_1 = ?, fault_2 = ?, fault_3 = ?, fault_4 = ?, fault_5 = ?
            WHERE machine_id = ?
        """, (
            machine_id,
            machine_name,
            ip_address,
            clean_text(payload.get("location")),
            clean_text(payload.get("shift_start_time")),
            clean_text(payload.get("shift_end_time")),
            is_active,
            clean_text(payload.get("fault_1")),
            clean_text(payload.get("fault_2")),
            clean_text(payload.get("fault_3")),
            clean_text(payload.get("fault_4")),
            clean_text(payload.get("fault_5")),
            old_machine_id
        ))

        # row = conn.execute("""
        #     SELECT date
        #     FROM machine_daily_cumulative
        #     WHERE machine_id = ?
        #     ORDER BY date DESC
        #     LIMIT 1
        # """, (machine_id,)).fetchone()

    return {"success": True}

@app.get("/api/settings/machines")
def get_machines():
    with get_settings_db() as conn:
        rows = conn.execute("""
            SELECT
                m.machine_id AS id,
                m.machine_id,
                m.machine_name,
                m.ip_address AS plc_address,
                m.location,
                m.shift_start_time,
                m.shift_end_time,
                m.is_active,
                m.fault_1, m.fault_2, m.fault_3, m.fault_4, m.fault_5
            FROM machine_master m
            ORDER BY m.machine_id
        """).fetchall()

    return [dict(r) for r in rows]


@app.post("/api/settings/machine")
def add_machine(payload: dict = Body(...)):

    machine_id, machine_name, ip_address = parse_machine_payload(payload)
    is_active = 1 if payload.get("is_active", True) else 0

    try:
        with get_settings_db() as conn:
            conn.execute("""
                INSERT INTO machine_master (
                    machine_id,
                    machine_name,
                    ip_address,
                    location,
                    shift_start_time,
                    shift_end_time,
                    is_active,
                    fault_1,
                    fault_2,
                    fault_3,
                    fault_4,
                    fault_5
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                machine_id,
                machine_name,
                ip_address,
                clean_text(payload.get("location")),
                clean_text(payload.get("shift_start_time")),
                clean_text(payload.get("shift_end_time")),
                is_active,
                clean_text(payload.get("fault_1")),
                clean_text(payload.get("fault_2")),
                clean_text(payload.get("fault_3")),
                clean_text(payload.get("fault_4")),
                clean_text(payload.get("fault_5")),
            ))

    except sqlite3.IntegrityError:
        raise HTTPException(
            status_code=400,
            detail="Machine ID already exists. Please use a different Machine ID."
        )

    return {
        "success": True,
        "message": "Machine added successfully"
    }

@app.delete("/api/settings/machine/{machine_id}")
def delete_machine(machine_id: int):
    """
    Deletes a machine from:
    - machine_master
    - machine_daily_cumulative
    """

    with get_settings_db() as settings:
        # delete from machine master
        settings.execute(
            "DELETE FROM machine_master WHERE machine_id = ?",
            (machine_id,)
        )

        # delete cumulative snapshots
        settings.execute(
            "DELETE FROM machine_daily_cumulative WHERE machine_id = ?",
            (machine_id,)
        )

    return {
        "success": True,
        "message": f"Machine {machine_id} deleted successfully"
    }


machine_status_cache = {}
machines_cache = []

def load_active_machines():
    with get_settings_db() as conn:
        rows = conn.execute("""
            SELECT machine_id, ip_address
            FROM machine_master
            WHERE is_active = 1
        """).fetchall()

    return [
        {"id": r["machine_id"], "ip": clean_text(r["ip_address"])}
        for r in rows
    ]

def update_machine_status():
    global machines_cache

    while True:
        try:
            machines_cache = load_active_machines()

            for m in machines_cache:
                plc_status, live_payload = check_device(m["id"], m["ip"])

                machine_status_cache[m["id"]] = {
                    "plc_status": plc_status,
                    "live_payload": live_payload,
                    "last_update": datetime.now()
                }

        except Exception as e:
            print("Status thread error:", e)

        time.sleep(1)   # every 1 sec


@app.get("/api/machines/status")
def machines_status():

    with get_settings_db() as sdb:
        machines = sdb.execute("""
            SELECT
                machine_id,
                machine_name,
                ip_address AS plc_address,
                location,
                is_active
            FROM machine_master
            WHERE is_active = 1
            ORDER BY machine_id
        """).fetchall()

    result = []

    with get_db() as odb:
        has_event_log = table_exists(odb, "event_log")

        # ✅ RUN QUERY ONLY ONCE
        latest_states = {}

        if has_event_log:
            rows = odb.execute("""
                SELECT e.machine_id, e.new_state
                FROM event_log e
                INNER JOIN (
                    SELECT machine_id, MAX(timestamp) as max_time
                    FROM event_log
                    GROUP BY machine_id
                ) latest
                ON e.machine_id = latest.machine_id
                AND e.timestamp = latest.max_time
            """).fetchall()

            latest_states = {
                r["machine_id"]: r["new_state"]
                for r in rows
            }

        # ✅ LOOP ONLY USES CACHE
        for m in machines:

            cached = machine_status_cache.get(m["machine_id"], {})
            plc_status = cached.get("plc_status", "OFFLINE")
            live_payload = cached.get("live_payload")

            # ✅ FAST lookup (no DB hit)
            last_event_state = latest_states.get(m["machine_id"])

            direct_state = extract_machine_state(live_payload)

            machine_state = resolve_machine_state(
                last_event_state,
                direct_state,
                plc_status
            )

            result.append({
                "machine_id": m["machine_id"],
                "machine_name": m["machine_name"],
                "plc_address": m["plc_address"],
                "location": m["location"],
                "plc_status": plc_status,
                "machine_state": machine_state,
                "status": machine_state,
                "last_event_state": last_event_state or "UNKNOWN",
            })

    return result


def _compute_live_oee(machine_id: int, runtime_sec: int, idle_sec: int, fault_sec: int, setting_sec: int) -> dict:
    """Compute full OEE metrics inline from raw ESP32 seconds. No DB read for counters."""
    today = date.today().isoformat()
    today_start = f"{today} 00:00:00"
    today_end   = f"{today} 23:59:59"

    # Stop / fault event counts from event_log (state-change pushes from ESP32)
    with get_db() as conn:
        counts = conn.execute("""
            SELECT
                SUM(CASE WHEN new_state = 'IDLE'  THEN 1 ELSE 0 END) AS idle_count,
                SUM(CASE WHEN new_state = 'FAULT' THEN 1 ELSE 0 END) AS fault_count
            FROM event_log
            WHERE machine_id = ? AND timestamp >= ? AND timestamp <= ?
        """, (machine_id, today_start, today_end)).fetchone()

    idle_count  = (counts["idle_count"]  or 0) if counts else 0
    fault_count = (counts["fault_count"] or 0) if counts else 0

    # Shift hours → planned production time
    with get_settings_db() as sdb:
        row = sdb.execute("""
            SELECT shift_start_time, shift_end_time
            FROM machine_master WHERE machine_id = ?
        """, (machine_id,)).fetchone()
    planned_sec = _calc_shift_seconds(row["shift_start_time"], row["shift_end_time"]) if row else 0

    # Today's power-off seconds — shift-clipped (mirrors history logic)
    shift_start_str = row["shift_start_time"] if row else None
    shift_end_str   = row["shift_end_time"]   if row else None
    with get_db() as conn:
        po_rows = conn.execute("""
            SELECT from_time, to_time FROM poweroff_log
            WHERE machine_id = ? AND date(from_time) = date('now', 'localtime')
        """, (machine_id,)).fetchall()
    poweroff_sec = sum(
        _poweroff_in_shift(r["from_time"], r["to_time"], shift_start_str, shift_end_str)
        for r in po_rows
    )

    run  = max(runtime_sec, 0)
    idle = max(idle_sec,    0)
    err  = max(fault_sec,   0)
    setting = max(setting_sec,   0)
    total = run + idle + err + setting

    availability      = run / planned_sec          if planned_sec > 0 else 0
    Error_time        = (err + poweroff_sec) / planned_sec if planned_sec > 0 else 0
    Idle_Time         = idle / planned_sec         if planned_sec > 0 else 0
    Utiliization_Rate = run / total                if total > 0       else 0

    return {
        "availability":      round(availability, 4),
        "Error_time":        round(Error_time, 4),
        "Idle_Time":         round(Idle_Time, 2),
        "Utiliization_Rate": round(Utiliization_Rate, 4),
        "runtime_sec":       run,
        "idle_sec":          idle,
        "fault_sec":         err,
        "setting_sec":       setting,
        "planned_time_sec":  planned_sec,
        "stop_count":        int(idle_count),
        "fault_count":       int(fault_count),
        "total_time":        total,
        "poweroff_sec":      poweroff_sec,
    }


@app.get("/api/sync_runtime_live")
def sync_runtime_live(machine_id: int):
    with get_settings_db() as conn:
        row = conn.execute(
            "SELECT ip_address FROM machine_master WHERE machine_id=?",
            (machine_id,)
        ).fetchone()

    if not row:
        raise HTTPException(status_code=404, detail="Machine not found")

    ip = row["ip_address"]

    # ── Single request: daily CSV totals + live current-hour counters ──
    # /live_summary combines what was previously two separate requests
    # (/daily_summary + /last_minute), eliminating the race condition.
    try:
        r = requests.get(f"http://{ip}:8080/live_summary", timeout=7)
        data = r.json()
        # daily_* = sum of completed hours from SD card CSV
        # live_*  = current in-progress hour counters (second accuracy, reset each hour)
        runtime_sec = int(data.get("daily_run",   0)) + int(data.get("live_run",   0))
        idle_sec    = int(data.get("daily_idle",  0)) + int(data.get("live_idle",  0))
        fault_sec   = int(data.get("daily_error", 0)) + int(data.get("live_error", 0))
        setting_sec   = int(data.get("daily_setting", 0)) + int(data.get("live_setting", 0))

    except Exception:
        # ESP32 offline — return last-good cached OEE (no DB read = no lock contention)
        _sync_poweroff_direct(machine_id, ip)
        cached = _runtime_cache.get(machine_id)
        if cached:
            result = _compute_live_oee(
                machine_id,
                cached["runtime_sec"],
                cached["idle_sec"],
                cached["fault_sec"],
                cached["setting_sec"],
            )
            result["status"] = "stale"
        else:
            result = _compute_live_oee(machine_id, 0, 0, 0, 0)
            result["status"] = "offline"
        return result

    # ── Cache fresh values in memory (offline fallback, zero DB contention) ──
    _runtime_cache[machine_id] = {
        "runtime_sec": runtime_sec,
        "idle_sec":    idle_sec,
        "fault_sec":   fault_sec,
        "setting_sec":   setting_sec,
    }

    # ── Sync poweroff events (throttled) ──
    _sync_poweroff_direct(machine_id, ip)

    # ── Compute and return full OEE inline ──
    result = _compute_live_oee(machine_id, runtime_sec, idle_sec, fault_sec, setting_sec)
    result["status"] = "synced"
    return result

@app.get("/api/oee/history")
def oee_history(
    machine_id: int,
    from_date: str = Query(...),
    to_date: str = Query(...)
):
    # Load from runtime_daily table (populated when ESP32 uploads CSV files)
    with get_db() as conn:
        rows = conn.execute("""
            SELECT date, runtime_seconds, idle_seconds, fault_seconds, setting_seconds
            FROM runtime_daily
            WHERE machine_id = ? AND date BETWEEN ? AND ?
            ORDER BY date ASC
        """, (machine_id, from_date, to_date)).fetchall()

    start = datetime.strptime(from_date, "%Y-%m-%d")
    end   = datetime.strptime(to_date,   "%Y-%m-%d")
    requested_days = (end - start).days + 1
    available_days = len(rows)

    all_dates = {r["date"] for r in rows}
    missing_days = []
    d = start
    while d <= end:
        ds = d.strftime("%Y-%m-%d")
        if ds not in all_dates:
            missing_days.append(ds)
        d += timedelta(days=1)

    if available_days == 0:
        return {"status": "NO DATA", "requested_days": requested_days}

    total_runtime = sum(r["runtime_seconds"] or 0 for r in rows)
    total_idle    = sum(r["idle_seconds"]    or 0 for r in rows)
    total_fault   = sum(r["fault_seconds"]   or 0 for r in rows)
    total_setting = sum(r["setting_seconds"]   or 0 for r in rows)
    total_time    = total_runtime + total_idle + total_fault + total_setting

    with get_settings_db() as sdb:
        machine = sdb.execute(
            "SELECT shift_start_time, shift_end_time FROM machine_master WHERE machine_id=?",
            (machine_id,)
        ).fetchone()

    shift_sec = _calc_shift_seconds(
        machine["shift_start_time"] if machine else None,
        machine["shift_end_time"]   if machine else None,
    )
    planned_time = shift_sec * available_days

    availability  = total_runtime / planned_time if planned_time > 0 else 0
    error_time    = total_fault   / planned_time if planned_time > 0 else 0
    idle_time_rat = total_idle    / planned_time if planned_time > 0 else 0
    utilization   = total_runtime / total_time   if total_time   > 0 else 0

    # Power-off per-day: read directly from M{mid}P{MMDD}.CSV files.
    # Only seconds that fall inside the shift window are counted.
    shift_start_str = machine["shift_start_time"] if machine else None
    shift_end_str   = machine["shift_end_time"]   if machine else None

    poweroff_days = []
    total_poweroff_sec = 0
    poweroff_available_days = 0
    d = start
    while d <= end:
        ds = d.strftime("%Y-%m-%d")
        file_present, duration = _read_poweroff_csv_for_date(
            machine_id, ds, shift_start_str, shift_end_str
        )
        if file_present:
            total_poweroff_sec += duration
            poweroff_available_days += 1
        poweroff_days.append({
            "date":         ds,
            "file_present": file_present,
            "poweroff_sec": duration if file_present else None,
        })
        d += timedelta(days=1)

    # Option B: denominator = planned time only for days with power-off CSV
    poweroff_planned_sec = shift_sec * poweroff_available_days

    return {
        "availability":           round(availability,  4),
        "Error_time":             round(error_time,    4),
        "Idle_Time":              round(idle_time_rat, 4),
        "Utiliization_Rate":      round(utilization,   4),
        "runtime_sec":            int(total_runtime),
        "idle_sec":               int(total_idle),
        "fault_sec":              int(total_fault),
        "setting_sec":            int(total_setting),
        "total_time":             int(total_time),
        "planned_time_sec":       int(planned_time),
        "requested_days":         requested_days,
        "available_days":         available_days,
        "missing_days":           missing_days,
        "poweroff_days":          poweroff_days,
        "total_poweroff_sec":     total_poweroff_sec,
        "poweroff_available_days": poweroff_available_days,
        "poweroff_planned_sec":   int(poweroff_planned_sec),
    }

@app.get("/api/machine_state")
def machine_state(machine_id: int, state: str, fault_code: int = 0):

    state = state.upper()
    fault_code = int(fault_code or 0)

    if state == "FAULT" and fault_code:
        mcu.set_latest_fault_code(machine_id, fault_code)

    if state == "FAULT" and fault_code == 0:
        fault_code = mcu.get_latest_fault_code(machine_id)

    with get_db() as conn:

        last_row = conn.execute("""
            SELECT new_state
            FROM event_log
            WHERE machine_id = ?
            ORDER BY id DESC
            LIMIT 1
        """, (machine_id,)).fetchone()

        old_state = last_row["new_state"] if last_row else "UNKNOWN"

        if old_state != state:

            mcu.log_event(
                machine_id,
                old_state,
                state,
                fault_code
            )

        elif state == "FAULT" and fault_code:

            conn.execute("""
                UPDATE fault_log
                SET fault_code = ?
                WHERE id = (
                    SELECT id
                    FROM fault_log
                    WHERE machine_id = ?
                    AND end_time IS NULL
                    ORDER BY id DESC
                    LIMIT 1
                )
            """, (fault_code, machine_id))

            conn.execute("""
                UPDATE event_log
                SET fault_code = ?
                WHERE id = (
                    SELECT id
                    FROM event_log
                    WHERE machine_id = ?
                    AND new_state = 'FAULT'
                    ORDER BY id DESC
                    LIMIT 1
                )
            """, (fault_code, machine_id))

    return {"status": "ok"}

    
@app.post("/shutdown")
@app.post("/api/shutdown")
def shutdown_backend():
    SHUTDOWN_EVENT.set()
    mcu.STOP_EVENT.set()
    return {"status": "backend stopping"}

# http://127.0.0.1:8000/api/oee/live?machine_id=1


# ESP32 (CSV uploads + time endpoint)
@app.post("/upload_csv")
async def upload_csv(filename: str, request: Request):
    body = await request.body()
    path = os.path.join(UPLOAD_DIR, filename)

    with open(path, "wb") as f:
        f.write(body)

    mcu.process_csv_files()  # writes to runtime_daily only

    # If this is a power-off file (M01P0320.CSV pattern), record its receipt.
    import re
    m = re.match(r'^M(\d+)P(\d{2})(\d{2})\.CSV$', filename.upper())
    if m:
        mid  = int(m.group(1))
        mm   = int(m.group(2))
        dd   = int(m.group(3))
        date_str = f"{datetime.now().year}-{mm:02d}-{dd:02d}"
        with get_settings_db() as conn:
            conn.execute("""
                INSERT OR REPLACE INTO poweroff_file_log (machine_id, date, filename)
                VALUES (?, ?, ?)
            """, (mid, date_str, filename))

    return {"status": "ok", "file": filename}


def _update_runtime_from_csv(filename: str):
    # Parse machine_id and date from filename e.g. m01_0310.csv
    try:
        # machine code prefix → machine_id (m01 → 1)
        prefix = filename.split("_")[0]   # "m01"
        machine_id = int(prefix[1:])      # 1

        part = filename.split("_")[1].split(".")[0]  # "0310"
        mm = int(part[:2])
        dd = int(part[2:4])
        year = datetime.now().year
        csv_date = f"{year}-{mm:02d}-{dd:02d}"
    except Exception as e:
        print(f"[CSV] Failed to parse filename {filename}: {e}")
        return

    # Read the uploaded CSV and sum all rows
    path = os.path.join(UPLOAD_DIR, filename)
    run_total = 0.0
    idle_total = 0.0
    fault_total = 0.0
    setting_total = 0.0

    try:
        with open(path, "r") as f:
            lines = f.readlines()

        for line in lines[1:]:   # skip header
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 4:
                continue
            run_total   += float(parts[1])
            idle_total  += float(parts[2])
            fault_total += float(parts[3])
            if len(parts) >= 5:
                setting_total += float(parts[4])

    except Exception as e:
        print(f"[CSV] Failed to read {filename}: {e}")
        return

    # Values from CSV are already in seconds (named run_min but actually seconds in your code)
    runtime_sec = int(run_total)
    idle_sec    = int(idle_total)
    fault_sec   = int(fault_total)
    setting_sec = int(setting_total)

    print(f"[CSV] updated: machine={machine_id} run={runtime_sec}s idle={idle_sec}s fault={fault_sec}s setting={setting_sec}s")


@app.get("/api/time")
def api_time():
    now = datetime.now()
    return {
        "year":   now.year,
        "month":  now.month,
        "day":    now.day,
        "hour":   now.hour,
        "minute": now.minute,
        "second": now.second
    }


@app.get("/api/last_uploaded_csv")
def last_uploaded_csv(request: Request):
    caller_ip = request.client.host
    with get_settings_db() as conn:
        row = conn.execute(
            "SELECT machine_id FROM machine_master WHERE ip_address = ?",
            (caller_ip,)
        ).fetchone()
    if not row:
        raise HTTPException(status_code=404, detail=f"No machine found for IP {caller_ip}")
    machine_id = row["machine_id"]
    prefix = f"m{machine_id:02d}_"
    latest_month = 0
    latest_day = 0
    for name in os.listdir(UPLOAD_DIR):
        if not name.lower().startswith(prefix):
            continue
        if not name.lower().endswith(".csv"):
            continue
        try:
            part = name.split("_")[1].split(".")[0]
            mm = int(part[:2])
            dd = int(part[2:4])
        except:
            continue
        if (mm, dd) > (latest_month, latest_day):
            latest_month = mm
            latest_day = dd
    return {"month": latest_month, "day": latest_day}


def _sync_poweroff_direct(machine_id: int, ip: str) -> None:
    """Fetch poweroff events from ESP32 and persist to DB.

    Throttled to _POWEROFF_SYNC_INTERVAL seconds so that frequent
    sync_runtime_live polls do NOT open a new ESP32 socket every call.
    Safe to call inline — no self-HTTP loopback.
    """
    now = time.time()
    if now - _poweroff_last_sync.get(machine_id, 0) < _POWEROFF_SYNC_INTERVAL:
        return                                      # throttled – skip this poll
    _poweroff_last_sync[machine_id] = now
    print(f"[_sync_poweroff_direct] ok — machine={machine_id} time={datetime.now().strftime('%H:%M:%S')}")

    try:
        r = requests.get(f"http://{ip}:8080/api/poweroff_events", timeout=5)
        events = r.json()
    except Exception:
        return                                      # ESP32 unreachable – try next interval
    if not isinstance(events, list):
        return

    with get_db() as conn:
        for ev in events:
            from_time = ev.get("from")
            to_time   = ev.get("to")
            if not from_time or not to_time:
                continue
            try:
                f = datetime.strptime(from_time, "%Y-%m-%d %H:%M:%S")
                t = datetime.strptime(to_time,   "%Y-%m-%d %H:%M:%S")
                duration = int((t - f).total_seconds())
            except Exception:
                continue
            exists = conn.execute("""
                SELECT 1 FROM poweroff_log
                WHERE machine_id=? AND from_time=? AND to_time=?
            """, (machine_id, from_time, to_time)).fetchone()
            if exists:
                continue
            conn.execute("""
                INSERT INTO poweroff_log (machine_id, from_time, to_time, duration_sec)
                VALUES (?, ?, ?, ?)
            """, (machine_id, from_time, to_time, duration))

            print("ok")