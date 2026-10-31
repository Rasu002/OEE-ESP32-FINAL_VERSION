from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
import httpx

app = FastAPI()

# ---- CORS (needed for React) ----
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ---- ESP32 CONFIG ----
ESP32_BASE_URL = "http://10.78.33.15"  # verify IP is static!

VALID_RELAYS = {1, 2}
VALID_STATES = {"ON", "OFF"}

# ---- API ----
@app.post("/relay")
async def set_relay(relay: int, state: str):
    state = state.upper()

    if relay not in VALID_RELAYS:
        raise HTTPException(status_code=400, detail="Invalid relay number")

    if state not in VALID_STATES:
        raise HTTPException(status_code=400, detail="Invalid relay state")

    params = {f"relay{relay}": state}

    async with httpx.AsyncClient(timeout=5) as client:
        try:
            r = await client.get(f"{ESP32_BASE_URL}/relay", params=params)
            r.raise_for_status()
        except httpx.RequestError as e:
            raise HTTPException(status_code=502, detail=f"ESP32 unreachable: {e}")
        except httpx.HTTPStatusError:
            raise HTTPException(status_code=500, detail="ESP32 error response")

    return {
        "relay": relay,
        "state": state,
        "status": "ok"
    }

@app.get("/status")
async def get_status():
    async with httpx.AsyncClient(timeout=5) as client:
        try:
            r = await client.get(f"{ESP32_BASE_URL}/status")
            r.raise_for_status()
            return r.json()
        except httpx.RequestError as e:
            raise HTTPException(status_code=502, detail=f"ESP32 unreachable: {e}")
