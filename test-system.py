import requests
import time
import statistics

URL = "http://127.0.0.1:8000/commands"

latencies = []
success = 0
fail = 0

NUM_REQUESTS = 50

print("Running system test...\n")

for i in range(NUM_REQUESTS):
    start = time.time()

    try:
        r = requests.post(URL, json={
            "command_name": "AUTO_TEST",
            "subsystem": "gnc",
            "args": {"i": i}
        })

        latency = (time.time() - start) * 1000

        if r.status_code == 200:
            latencies.append(latency)
            success += 1
            print(f"[OK] req={i} latency={latency:.2f} ms")
        else:
            fail += 1
            print(f"[FAIL] req={i}")

    except Exception:
        fail += 1
        print(f"[ERROR] req={i}")

    time.sleep(0.05)

print("\n===== SYSTEM METRICS =====")

print(f"Total Requests: {NUM_REQUESTS}")
print(f"Success: {success}")
print(f"Fail: {fail}")
print(f"Success Rate: {100 * success / NUM_REQUESTS:.2f}%")

if latencies:
    print(f"Avg Latency: {statistics.mean(latencies):.2f} ms")
    print(f"P95 Latency: {statistics.quantiles(latencies, n=20)[-1]:.2f} ms")
    print(f"Max Latency: {max(latencies):.2f} ms")