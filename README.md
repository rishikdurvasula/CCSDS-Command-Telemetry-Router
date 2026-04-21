CCSDS Command & Telemetry Router

A production-grade simulation of a spacecraft ground station communication system implementing the CCSDS Space Packet Protocol (CCSDS 133.0-B) with fault-tolerant transport, real-time telemetry streaming, and automated performance testing.

📌 Overview

This project models how real spacecraft communication systems operate under unreliable network conditions. It supports:

Telecommand (TC) transmission with reliability guarantees
Telemetry (TM) streaming across multiple subsystems
Packet loss, corruption, and reordering simulation
ACK-based retransmission with exponential backoff
Binary packet encoding/decoding using CCSDS standards
⚡ Key Results
✅ 100% command success rate
⚡ 4.46 ms average latency
📉 10.7 ms P95 latency
🔁 Reliable delivery over lossy UDP channels
🧠 System Architecture
User → FastAPI → UDP → C++ Sender → Lossy Channel → C++ Receiver
                                      ↓
                                ACK + Retransmit
⚙️ Components
Component	Description
C++ Core	CCSDS packet encoding/decoding, UDP transport, retransmission logic
Lossy Channel	Simulates packet drop, corruption, and reordering
FastAPI	Command injection interface
Receiver	Decodes packets, detects gaps, sends ACKs
Test Harness	Measures latency, success rate, system performance
🛰️ Features
✅ CCSDS Packet Protocol (133.0-B)
Primary header fields (APID, sequence flags, sequence count)
Binary packet framing
CRC-16/CCITT validation
Round-trip encode/decode testing
🌐 Fault Injection
Configurable packet loss
Bit-level corruption
Packet reordering
🔁 Reliability Layer
ACK packets for telecommands
Retransmission with exponential backoff
In-flight command tracking
📡 Telemetry Simulation
Multi-channel telemetry:
GNC (high priority)
Propulsion (high priority)
Power (medium)
Thermal (low)
Priority-based scheduling under load
🛠️ Setup & Run
1. Clone repository
git clone <your-repo-url>
cd ccsds-router
2. Build the system
mkdir build
cd build
cmake ..
cmake --build .
3. Run components (in separate terminals)
Terminal 1 — Receiver
./udp_receiver
Terminal 2 — Sender
./udp_sender
Terminal 3 — API
cd python_api
python3 -m venv venv
source venv/bin/activate
pip install fastapi uvicorn requests
uvicorn app.main:app --reload
4. Send a command
curl -X POST http://127.0.0.1:8000/commands \
  -H "Content-Type: application/json" \
  -d '{
    "command_name": "TEST_CMD",
    "subsystem": "gnc",
    "args": {"mode": 1}
  }'
🧪 Automated Testing

Run the system test:

python3 test_system.py
Example Output
===== SYSTEM METRICS =====
Total Requests: 50
Success: 50
Fail: 0
Success Rate: 100.00%
Avg Latency: 4.46 ms
P95 Latency: 10.73 ms
Max Latency: 12.79 ms
🎮 Chaos Control (Runtime)

In the sender terminal:

drop 0.7       # simulate packet loss
corrupt 0.3    # simulate corruption
reorder 0.5    # simulate reordering
show           # view current config
📊 What This Demonstrates
Reliable communication over unreliable transport (UDP)
Binary protocol implementation (CCSDS)
Real-time systems behavior under failure conditions
Distributed system concepts:
retransmission
exponential backoff
fault tolerance
state tracking
🔧 Tech Stack
C++20 — core systems, packet handling, networking
Python (FastAPI) — API layer and testing
UDP Sockets — transport layer
CMake — build system
🚀 Future Work
InfluxDB + Grafana dashboards for real-time telemetry visualization
Docker Compose deployment (one-command startup)
gRPC control plane
Kubernetes deployment for scalability
High-throughput benchmarking (10K+ packets/sec)
