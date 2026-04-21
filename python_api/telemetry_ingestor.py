import socket
from influxdb_client import InfluxDBClient, Point
import time

client = InfluxDBClient(
    url="http://localhost:8086",
    token="my-token",
    org="ccsds"
)

write_api = client.write_api()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 9200))

print("Telemetry ingestor listening on 9200...")

while True:
    data, _ = sock.recvfrom(4096)

    point = (
        Point("telemetry")
        .field("packet_size", len(data))
        .time(time.time_ns())
    )

    write_api.write(bucket="telemetry", record=point)