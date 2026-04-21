from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field
import socket
import struct
import time

app = FastAPI(title="CCSDS Command API")

SENDER_HOST = "127.0.0.1"
SENDER_CONTROL_PORT = 9100


class CommandRequest(BaseModel):
    command_name: str = Field(..., min_length=1, max_length=64)
    subsystem: str = Field(..., min_length=1, max_length=32)
    args: dict = Field(default_factory=dict)


class CommandResponse(BaseModel):
    status: str
    bytes_sent: int
    command_name: str
    subsystem: str
    injected_at_ms: int


def encode_command(req: CommandRequest) -> bytes:
    """
    Very small project-specific command payload for Part 6.

    Format:
      [1 byte subsystem_len]
      [N bytes subsystem utf-8]
      [1 byte command_len]
      [M bytes command utf-8]
      [2 bytes arg_blob_len big-endian]
      [arg_blob utf-8]

    arg_blob is a compact key=value;key=value string for now.
    """
    subsystem_b = req.subsystem.encode("utf-8")
    command_b = req.command_name.encode("utf-8")
    arg_blob = ";".join(f"{k}={v}" for k, v in req.args.items()).encode("utf-8")

    if len(subsystem_b) > 255:
        raise ValueError("subsystem too long")
    if len(command_b) > 255:
        raise ValueError("command_name too long")
    if len(arg_blob) > 65535:
        raise ValueError("args blob too long")

    payload = bytearray()
    payload.append(len(subsystem_b))
    payload.extend(subsystem_b)
    payload.append(len(command_b))
    payload.extend(command_b)
    payload.extend(struct.pack(">H", len(arg_blob)))
    payload.extend(arg_blob)
    return bytes(payload)


@app.get("/health")
def health():
    return {"status": "ok"}


@app.post("/commands", response_model=CommandResponse)
def send_command(req: CommandRequest):
    try:
        payload = encode_command(req)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sent = sock.sendto(payload, (SENDER_HOST, SENDER_CONTROL_PORT))
    finally:
        sock.close()

    return CommandResponse(
        status="injected",
        bytes_sent=sent,
        command_name=req.command_name,
        subsystem=req.subsystem,
        injected_at_ms=int(time.time() * 1000),
    )