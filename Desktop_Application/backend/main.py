import multiprocessing
import sys
import threading
import time

import api
import uvicorn

try:
    import mcu_run as mcu
except ModuleNotFoundError:
    from server.backend import mcu_run as mcu  # type: ignore


def plc_runner():
    mcu.init_db()
    mcu.load_last_states()
    mcu.process_csv_files()
    mcu.main_loop(mcu.STOP_EVENT)


def api_runner(server):
    server.run()


def watch_shutdown(server):
    while not api.SHUTDOWN_EVENT.is_set():
        time.sleep(0.5)

    mcu.STOP_EVENT.set()
    server.should_exit = True


if __name__ == "__main__":

    multiprocessing.freeze_support()

    plc_thread = threading.Thread(target=plc_runner, daemon=True)
    plc_thread.start()

    config = uvicorn.Config(
        api.app,
        host="0.0.0.0",
        port=8000,
        workers=1,
    )
    server = uvicorn.Server(config)

    watcher = threading.Thread(target=watch_shutdown, args=(server,), daemon=True)
    watcher.start()

    api_thread = threading.Thread(target=api_runner, args=(server,), daemon=True)
    api_thread.start()

    try:
        while api_thread.is_alive():
            api_thread.join(timeout=1)
    except KeyboardInterrupt:
        print("Stopping system...")
    finally:
        api.SHUTDOWN_EVENT.set()
        mcu.STOP_EVENT.set()
        server.should_exit = True

        api_thread.join(timeout=5)
        plc_thread.join(timeout=5)

    sys.exit(0)
