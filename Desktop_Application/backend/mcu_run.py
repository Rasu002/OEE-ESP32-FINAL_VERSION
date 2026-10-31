# mcu_run.py
from datetime import datetime
import json
import os
import sqlite3
import threading
import time
import sys
import urllib.request
import urllib.error
import socket
import logging
import csv


CSV_DIR = "csv_uploads"
DB_PATH = "oee.db"


LOG_FILE = os.path.join(os.path.dirname(__file__), "mcu_connection.log")
_last_state_map = {}
_state_lock = threading.Lock()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(message)s",
    handlers=[
        logging.FileHandler(LOG_FILE),
        logging.StreamHandler()
    ]
)

logger = logging.getLogger("MCU")


STOP_EVENT = threading.Event()

_latest_fault_codes_lock = threading.Lock()
_latest_fault_codes = {}

_connection_state_lock = threading.Lock()
_connection_state = {}

_last_machine_state = {}
_last_connection_state = {}

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


# ---------------- DATABASE ----------------
def init_db():
    conn = sqlite3.connect(app_path("oee.db"))
    cur = conn.cursor()

    cur.executescript("""
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
                      
    CREATE TABLE IF NOT EXISTS runtime_daily (
        machine_id INTEGER,
        date TEXT,
        runtime_seconds INTEGER DEFAULT 0,
        idle_seconds INTEGER DEFAULT 0,
        fault_seconds INTEGER DEFAULT 0,
        part_count INTEGER DEFAULT 0,
        planned_downtime INTEGER DEFAULT 0,
        ideal_cycle_time INTEGER,
        runtime_of_machine INTEGER,
        setting_seconds INTEGER DEFAULT 0,
        PRIMARY KEY (machine_id, date)
    );

    CREATE TABLE IF NOT EXISTS event_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT,
        machine_id INTEGER,
        event_type TEXT,
        old_state TEXT,
        new_state TEXT,
        fault_code INTEGER DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS fault_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        machine_id INTEGER,
        fault_code INTEGER,
        start_time TEXT,
        end_time TEXT,
        duration_sec INTEGER
    );
    """)

    # Backward-compatible schema upgrade for existing DBs.
    try:
        cols = {r[1] for r in cur.execute("PRAGMA table_info(runtime_daily)").fetchall()}
        if "runtime_seconds" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN runtime_seconds INTEGER DEFAULT 0")
        if "idle_seconds" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN idle_seconds INTEGER DEFAULT 0")
        if "fault_seconds" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN fault_seconds INTEGER DEFAULT 0")
        if "part_count" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN part_count INTEGER DEFAULT 0")
        if "planned_downtime" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN planned_downtime INTEGER DEFAULT 0")
        if "ideal_cycle_time" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN ideal_cycle_time INTEGER")
        if "runtime_of_machine" not in cols:
            cur.execute("ALTER TABLE runtime_daily ADD COLUMN runtime_of_machine INTEGER")
        if "setting_seconds" in cols:
            cur.execute(
                "UPDATE runtime_daily SET setting_seconds = 10"
    )
    except sqlite3.OperationalError:
        pass

    # Migration: add shift columns to machine_master for existing databases.
    try:
        mm_cols = {r[1] for r in cur.execute("PRAGMA table_info(machine_master)").fetchall()}
        if "shift_start_time" not in mm_cols:
            cur.execute("ALTER TABLE machine_master ADD COLUMN shift_start_time TEXT")
        if "shift_end_time" not in mm_cols:
            cur.execute("ALTER TABLE machine_master ADD COLUMN shift_end_time TEXT")
    except sqlite3.OperationalError:
        pass

    conn.commit()
    conn.close()


# ---------------- LOAD MACHINES ----------------
def load_machines():
    conn = sqlite3.connect(app_path("oee.db"))
    conn.row_factory = sqlite3.Row

    rows = conn.execute(
        "SELECT machine_id, ip_address FROM machine_master WHERE is_active=1"
    ).fetchall()

    conn.close()

    return {r["machine_id"]: r["ip_address"] for r in rows}


# ---------------- FETCH MCU DATA ----------------
def fetch_esp32_data(ip):

    url = f"http://{ip}:8080/last_minute"

    try:
        with urllib.request.urlopen(url, timeout=2) as resp:
            data = resp.read().decode("utf-8")
            return json.loads(data)

    except (urllib.error.URLError,
            urllib.error.HTTPError,
            ConnectionResetError,
            socket.timeout,
            UnicodeDecodeError,
            json.JSONDecodeError,
            ValueError,
            OSError):

        return None


def is_ip_connected(ip: str) -> bool:
    return fetch_esp32_data(ip) is not None


def check_ip_connection(machine_id: int, ip: str) -> tuple[bool, dict | None]:
    data = fetch_esp32_data(ip)
    connected = data is not None

    with _connection_state_lock:
        _connection_state[int(machine_id)] = connected

    # if connected:
    #     logger.info(f"Machine {machine_id} | {ip} | CONNECTED")
    # else:
    #     logger.warning(f"Machine {machine_id} | {ip} | TERMINATED")

    return connected, data


# ---------------- LOG EVENT ----------------
def log_event(machine_id, old_state, new_state, fault_code=0):
    machine_id = int(machine_id)
    fault_code = int(fault_code or 0)

    # Prefer the latest cached fault code so FAULT rows are not inserted with 0
    # when the code arrives slightly before/after the state transition.
    if new_state == "FAULT" and fault_code == 0:
        fault_code = get_latest_fault_code(machine_id, 0)

    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    conn = sqlite3.connect(app_path("oee.db"))
    cur = conn.cursor()

    # ---- insert event ----
    cur.execute(
        """INSERT INTO event_log
        (timestamp, machine_id, event_type, old_state, new_state, fault_code)
        VALUES (?, ?, ?, ?, ?, ?)""",
        (
            now,
            machine_id,
            f"{old_state}->{new_state}",
            old_state,
            new_state,
            fault_code
        )
    )

    # ---- FAULT START ----
    if new_state == "FAULT":

        cur.execute("""
            INSERT INTO fault_log
            (machine_id, fault_code, start_time)
            VALUES (?, ?, ?)
        """, (machine_id, fault_code, now))


    # ---- FAULT END ----
    if old_state == "FAULT" and new_state != "FAULT":

        row = cur.execute("""
            SELECT id, start_time
            FROM fault_log
            WHERE machine_id = ?
              AND end_time IS NULL
            ORDER BY id DESC
            LIMIT 1
        """, (machine_id,)).fetchone()

        if row:

            fault_id, start_time = row

            start_dt = datetime.strptime(start_time, "%Y-%m-%d %H:%M:%S")
            end_dt = datetime.strptime(now, "%Y-%m-%d %H:%M:%S")

            duration = int((end_dt - start_dt).total_seconds())

            cur.execute("""
                UPDATE fault_log
                SET end_time = ?, duration_sec = ?
                WHERE id = ?
            """, (now, duration, fault_id))

    conn.commit()
    conn.close()

    if new_state != "MCU_OFFLINE":
        _last_machine_state[machine_id] = new_state

# --------------------CSV data -> db--------------
def process_csv_file(path):

    conn = sqlite3.connect(app_path("oee.db"))
    cur = conn.cursor()

    filename = os.path.basename(path)

    try:
        part = filename.split("_")[1].split(".")[0]

        machine_id = int(filename[1:3])
        month = int(part[:2])
        day = int(part[2:4])

    except Exception:
        return

    run_total = 0
    idle_total = 0
    fault_total = 0
    year = None

    with open(path, newline="") as f:

        reader = csv.DictReader(f)

        for row in reader:

            ts = datetime.strptime(row["timestamp"], "%Y-%m-%d %H:%M:%S")

            if year is None:
                year = ts.year

            run_total   += float(row.get("run_sec")   or row.get("run_min",   0))
            idle_total  += float(row.get("idle_sec")  or row.get("idle_min",  0))
            fault_total += float(row.get("error_sec") or row.get("error_min", 0))

    if year is None:
        conn.close()
        return

    date_str = f"{year:04d}-{month:02d}-{day:02d}"

    # fetch machine settings
    row = cur.execute(
        """
        SELECT planned_downtime, ideal_cycle_time, shift_start_time, shift_end_time
        FROM machine_master
        WHERE machine_id=?
        """,
        (machine_id,)
    ).fetchone()

    planned_downtime = row[0] if row and row[0] is not None else 0
    ideal_cycle_time = row[1] if row and row[1] is not None else 0

    # Calculate runtime_of_machine (seconds) from shift window instead of
    # storing a raw number — handles overnight shifts automatically.
    def _shift_sec(start, end):
        if not start or not end:
            return 0
        try:
            from datetime import datetime, timedelta
            fmt = "%H:%M"
            s = datetime.strptime(start[:5], fmt)
            e = datetime.strptime(end[:5],   fmt)
            if e <= s:
                e += timedelta(days=1)
            return int((e - s).total_seconds())
        except Exception:
            return 0

    runtime_of_machine = _shift_sec(
        row[2] if row else None,
        row[3] if row else None,
    )

    cur.execute(
        """
        INSERT INTO runtime_daily
        (machine_id, date, runtime_seconds, idle_seconds, fault_seconds,
         part_count, planned_downtime, ideal_cycle_time, runtime_of_machine)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)

        ON CONFLICT(machine_id, date)
        DO UPDATE SET
            runtime_seconds=excluded.runtime_seconds,
            idle_seconds=excluded.idle_seconds,
            fault_seconds=excluded.fault_seconds,
            part_count=excluded.part_count,
            planned_downtime=excluded.planned_downtime,
            ideal_cycle_time=excluded.ideal_cycle_time,
            runtime_of_machine=excluded.runtime_of_machine
        """,
        (
            machine_id,
            date_str,
            int(run_total),
            int(idle_total),
            int(fault_total),
            1,
            int(planned_downtime),
            ideal_cycle_time,
            runtime_of_machine,
        ),
    )

    conn.commit()
    conn.close()


def process_csv_files():

    init_db()

    candidates = [
        app_path(CSV_DIR),
        os.path.join(os.getcwd(), CSV_DIR),
    ]

    for folder in candidates:
        if not os.path.isdir(folder):
            continue

        for name in os.listdir(folder):
            if not name.lower().endswith(".csv"):
                continue

            path = os.path.join(folder, name)
            process_csv_file(path)
# ---------------- MAIN LOOP ----------------
def main_loop(stop_event):

    logger.info("MCU polling started")

    while not stop_event.is_set():
        machines = load_machines()

        for machine_id, ip in machines.items():

            connected, _ = check_ip_connection(machine_id, ip)

            last_conn = _last_connection_state.get(machine_id)

            # first scan
            if last_conn is None:
                _last_connection_state[machine_id] = connected
                continue


            # ethernet disconnected
            if not connected and last_conn:

                last_state = _last_machine_state.get(machine_id, "IDLE")

                log_event(
                    machine_id,
                    last_state,
                    "MCU_OFFLINE"
                )


            # ethernet restored
            elif connected and not last_conn:

                # do NOT log anything
                # ESP32 will push real state
                pass


            _last_connection_state[machine_id] = connected

        time.sleep(0.5)

def set_latest_fault_code(machine_id: int, code: int) -> None:
    with _latest_fault_codes_lock:
        _latest_fault_codes[int(machine_id)] = int(code)


def get_latest_fault_code(machine_id: int, default: int = 0) -> int:
    with _latest_fault_codes_lock:
        return int(_latest_fault_codes.get(int(machine_id), int(default)))

def load_last_states():

    with sqlite3.connect(app_path("oee.db")) as conn:

        rows = conn.execute("""
            SELECT machine_id, new_state
            FROM event_log
            WHERE id IN (
                SELECT MAX(id)
                FROM event_log
                GROUP BY machine_id
            )
        """).fetchall()

    for machine_id, state in rows:
        _last_machine_state[machine_id] = state
        
if __name__ == "__main__":
    init_db()
    load_last_states()
    main_loop(STOP_EVENT)