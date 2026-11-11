import json
import unittest
from gateway.unifi_gateway import normalize_snapshot, UniFiApiClient, SnapshotStore

class FakeResponse:
    def __init__(self, payload, status=200): self.payload=payload; self.status=status
    def __enter__(self): return self
    def __exit__(self,*a): pass
    def read(self): return json.dumps(self.payload).encode()

class GatewayTests(unittest.TestCase):
    def test_normalizes_site_metrics_devices_sdwan_and_clients(self):
        out = normalize_snapshot(
            {"data":[{"id":"s1","name":"Home","status":"ONLINE","wanUptimeSec":86400,"internetIssues":["dns"],"isp":{"name":"Acme"},"onlineDevices":3,"onlineClients":7}]},
            {"data":[{"averageLatency":12.5,"maxLatency":44,"packetLossPct":0.3,"uploadBps":100,"downloadBps":1000,"timestamp":"2026-11-07T00:00:00Z","metric_age_sec":90}]},
            {"data":[{"name":"gw","online":True,"startupTime":"2026-11-06T00:00:00Z"},{"name":"ap","online":False}]},
            {"data":[{"id":"sd1","hubCount":1,"spokeCount":2,"totalTunnels":3,"primaryWan":"fiber","backupWan":"lte"}]},
            {"tunnels":[{"connected":True},{"connected":False}],"latencyMs":21,"warnings":["degraded"]},
            {"data":[{"name":"phone","type":"WIRELESS","ip":"10.0.0.2","connectedAt":"2026-11-07T01:00:00Z"},{"name":"vpn","type":"VPN","ip":"10.0.0.3"},{"name":"guest","isGuest":True,"type":"WIRED"},{"name":"tele","type":"TELEPORT"}]},
            client_error=None)
        self.assertEqual(out["site"]["name"], "Home")
        self.assertEqual(out["isp_metrics"]["metric_age_sec"], 90)
        self.assertEqual(out["devices"]["offline"], 1)
        self.assertEqual(out["sd_wan"]["connected_tunnels"], 1)
        self.assertEqual(out["clients"]["counts"]["wireless"], 1)
        self.assertEqual(out["clients"]["counts"]["vpn"], 1)
        self.assertEqual(out["clients"]["counts"]["teleport"], 1)
        self.assertEqual(out["clients"]["counts"]["guest"], 1)

    def test_connector_error_is_explicit_and_client_list_bounded(self):
        clients = {"data":[{"name":str(i),"type":"WIRED"} for i in range(20)]}
        out = normalize_snapshot({}, {}, {}, {}, clients=clients, client_error="HTTP 403")
        self.assertEqual(len(out["clients"]["items"]), 12)
        self.assertEqual(out["clients"]["error"], "HTTP 403")

    def test_api_client_sends_x_api_key(self):
        seen = {}
        def opener(request, timeout):
            seen["key"] = request.get_header("X-api-key")
            return FakeResponse({"data": []})
        self.assertEqual(UniFiApiClient("https://x", "secret", opener=opener).get("/v1/sites"), {"data": []})
        self.assertEqual(seen["key"], "secret")

if __name__ == "__main__": unittest.main()

class StoreTests(unittest.TestCase):
    def test_snapshot_store_calls_all_read_only_endpoints_and_keeps_site_total(self):
        class FakeClient:
            def __init__(self): self.paths=[]
            def get(self, path, params=None):
                self.paths.append(path)
                if path == "/v1/sites": return {"data":[{"id":"s1","name":"Lab","onlineClients":9}]}
                if path == "/v1/isp-metrics/5m": return {"data":[]}
                if path == "/v1/devices": return {"data":[]}
                if path == "/v1/sd-wan-configs": return {"data":[]}
                raise __import__('gateway.unifi_gateway', fromlist=['UniFiApiError']).UniFiApiError("HTTP 403")
        fake=FakeClient(); store=SnapshotStore(fake, host_id="h1")
        out=store.refresh()
        self.assertEqual(out["clients"]["counts"]["total"],9)
        self.assertEqual(out["clients"]["error"],"HTTP 403")
        self.assertIn("/v1/sites", fake.paths)
