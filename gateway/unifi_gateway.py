"""Local UniFi read-only gateway and compact snapshot normalizer.

The gateway owns the UniFi API key. Device clients only receive the normalized,
read-only snapshot returned by this module.
"""
from __future__ import annotations

import json
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional

DEFAULT_BASE_URL = "https://api.ui.com"
DEFAULT_POLL_INTERVAL = 30
MAX_CLIENTS = 12


def _items(payload: Any) -> List[Mapping[str, Any]]:
    if isinstance(payload, list):
        return [x for x in payload if isinstance(x, Mapping)]
    if isinstance(payload, Mapping):
        for key in ("data", "items", "results"):
            value = payload.get(key)
            if isinstance(value, list):
                return [x for x in value if isinstance(x, Mapping)]
    return []


def _first(obj: Mapping[str, Any], *keys: str, default: Any = None) -> Any:
    for key in keys:
        if key in obj and obj[key] is not None:
            return obj[key]
    return default


def _num(obj: Mapping[str, Any], *keys: str, default: float = 0.0) -> float:
    value = _first(obj, *keys, default=default)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _bool(obj: Mapping[str, Any], *keys: str, default: bool = False) -> bool:
    value = _first(obj, *keys, default=default)
    if isinstance(value, str):
        return value.lower() in ("true", "yes", "online", "connected", "up")
    return bool(value)


def _type_name(value: Any) -> str:
    return str(value or "UNKNOWN").upper().replace("-", "_")


def _iso_or_none(value: Any) -> Optional[str]:
    return str(value) if value not in (None, "") else None


def _client_type(client: Mapping[str, Any]) -> str:
    raw = _first(client, "type", "clientType", "connectionType", "osClass", default="UNKNOWN")
    kind = _type_name(raw)
    if "TELEPORT" in kind:
        return "TELEPORT"
    if "VPN" in kind or _bool(client, "isVpn", "vpn", default=False):
        return "VPN"
    if _bool(client, "isGuest", "guest", default=False):
        return "GUEST"
    if "WIRELESS" in kind or "WIFI" in kind or kind == "WLAN":
        return "WIRELESS"
    if "WIRED" in kind or "ETHERNET" in kind or kind == "LAN":
        return "WIRED"
    return kind


def normalize_snapshot(sites: Any, metrics: Any, devices: Any, sdwan: Any,
                       sdwan_status: Optional[Mapping[str, Any]] = None,
                       clients: Optional[Any] = None,
                       *, now: Optional[float] = None,
                       client_error: Optional[str] = None) -> Dict[str, Any]:
    """Normalize tolerant fake/real API responses into the device contract."""
    now = time.time() if now is None else now
    site_rows = _items(sites)
    site = site_rows[0] if site_rows else (sites if isinstance(sites, Mapping) else {})
    metric_rows = _items(metrics)
    metric = metric_rows[-1] if metric_rows else (metrics if isinstance(metrics, Mapping) else {})
    device_rows = _items(devices)
    wan = _first(site, "wan", "internet", "isp", default={})
    if not isinstance(wan, Mapping):
        wan = {}
    isp = _first(site, "isp", "internetServiceProvider", default={})
    if not isinstance(isp, Mapping):
        isp = {}

    online_devices = [d for d in device_rows if _bool(d, "isOnline", "online", "status", default=False)]
    offline_devices = [d for d in device_rows if d not in online_devices]
    sdwan_rows = _items(sdwan)
    sdwan_cfg = sdwan_rows[0] if sdwan_rows else (sdwan if isinstance(sdwan, Mapping) else {})
    status = sdwan_status if isinstance(sdwan_status, Mapping) else {}
    tunnels = _items(status.get("tunnels", status))
    connected_tunnels = sum(1 for t in tunnels if _bool(t, "connected", "isConnected", "status", default=False))
    total_tunnels = int(_num(status, "totalTunnels", "tunnelCount", default=len(tunnels)))

    client_rows = _items(clients)
    counts = {"total": len(client_rows), "wireless": 0, "wired": 0, "guest": 0, "vpn": 0, "teleport": 0}
    if clients is None:
        # Connector permissions can be absent; preserve the Site Manager total.
        counts["total"] = int(_num(site, "onlineClients", "clientsOnline", default=0))
    compact_clients: List[Dict[str, Any]] = []
    for row in client_rows:
        kind = _client_type(row)
        key = {"WIRELESS": "wireless", "WIRED": "wired", "GUEST": "guest", "VPN": "vpn", "TELEPORT": "teleport"}.get(kind)
        if key:
            counts[key] += 1
        compact_clients.append({
            "name": str(_first(row, "name", "hostname", "displayName", "mac", default="unknown")),
            "type": kind,
            "ip": str(_first(row, "ip", "ipAddress", "ipv4", default="-")),
            "connected_at": _iso_or_none(_first(row, "connectedAt", "connectionTime", "firstSeen", "startTime")),
        })
    compact_clients = compact_clients[:MAX_CLIENTS]

    metric_time = _first(metric, "timestamp", "time", "createdAt", "startTime")
    metric_age = _num(metric, "metric_age_sec", "ageSec", default=0.0)
    if metric_age == 0 and isinstance(metric_time, (int, float)):
        metric_age = max(0.0, now - float(metric_time))
    snapshot = {
        "schema": "unifi-gateway/v1",
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now)),
        "site": {
            "id": str(_first(site, "id", "siteId", default="")),
            "name": str(_first(site, "name", "siteName", "displayName", default="UniFi site")),
            "status": "online" if _bool(site, "isOnline", "online", "status", default=True) else "offline",
            "wan_uptime_sec": int(_num(site, "wanUptimeSec", "wan_uptime_sec", "uptime", default=_num(wan, "uptime", default=0))),
            "internet_issues": list(_first(site, "internetIssues", "issues", default=[] ) or []),
            "isp": {"name": str(_first(isp, "name", "ispName", default="-")), "status": str(_first(isp, "status", default="-"))},
            "online_devices": int(_num(site, "onlineDevices", "devicesOnline", default=len(online_devices))),
            "online_clients": int(_num(site, "onlineClients", "clientsOnline", default=counts["total"])),
        },
        "isp_metrics": {
            "average_latency_ms": _num(metric, "averageLatency", "avgLatency", "latencyMs", "latency", default=0),
            "max_latency_ms": _num(metric, "maxLatency", "latencyMax", default=0),
            "packet_loss_pct": _num(metric, "packetLoss", "packetLossPct", "lossPct", default=0),
            "upload_bps": _num(metric, "upload", "uploadBps", "upstreamBps", default=0),
            "download_bps": _num(metric, "download", "downloadBps", "downstreamBps", default=0),
            "uptime_sec": int(_num(metric, "uptime", "uptimeSec", default=0)),
            "metric_time": _iso_or_none(metric_time),
            "metric_age_sec": int(metric_age),
        },
        "devices": {
            "total": len(device_rows), "online": len(online_devices), "offline": len(offline_devices),
            "offline_names": [str(_first(d, "name", "hostname", "mac", default="unknown")) for d in offline_devices[:12]],
            "startup_times": [{"name": str(_first(d, "name", "hostname", default="unknown")), "startup_time": _iso_or_none(_first(d, "startupTime", "startTime"))} for d in online_devices[:12]],
        },
        "sd_wan": {
            "connected_tunnels": connected_tunnels or int(_num(sdwan_cfg, "connectedTunnels", default=0)),
            "total_tunnels": total_tunnels or int(_num(sdwan_cfg, "totalTunnels", default=0)),
            "hub_count": int(_num(sdwan_cfg, "hubCount", "hubs", default=len(_items(sdwan_cfg.get("hubs"))))),
            "spoke_count": int(_num(sdwan_cfg, "spokeCount", "spokes", default=len(_items(sdwan_cfg.get("spokes"))))),
            "primary_wan": str(_first(sdwan_cfg, "primaryWan", "mainWan", default="-")),
            "backup_wan": str(_first(sdwan_cfg, "backupWan", "secondaryWan", default="-")),
            "latency_ms": _num(status, "latency", "latencyMs", default=0),
            "errors": list(_first(status, "errors", "error", default=[] ) or []),
            "warnings": list(_first(status, "warnings", "warning", default=[] ) or []),
        },
        "clients": {"counts": counts, "items": compact_clients, "error": client_error},
    }
    return snapshot


class UniFiApiError(RuntimeError):
    pass


class UniFiApiClient:
    def __init__(self, base_url: str, api_key: str, opener: Optional[Callable[..., Any]] = None,
                 timeout: float = 8.0, retries: int = 2):
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key
        self.timeout = timeout
        self.retries = retries
        self.opener = opener or urllib.request.urlopen

    def get(self, path: str, params: Optional[Mapping[str, str]] = None) -> Any:
        url = self.base_url + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        request = urllib.request.Request(url, headers={"X-API-KEY": self.api_key, "Accept": "application/json"})
        last: Optional[Exception] = None
        for attempt in range(self.retries + 1):
            try:
                with self.opener(request, timeout=self.timeout) as response:
                    if getattr(response, "status", 200) == 429:
                        raise UniFiApiError("HTTP 429 rate limited")
                    if getattr(response, "status", 200) >= 400:
                        raise UniFiApiError(f"HTTP {response.status}")
                    return json.loads(response.read().decode("utf-8"))
            except (urllib.error.URLError, TimeoutError, ValueError, UniFiApiError) as exc:
                last = exc
                if "429" not in str(exc) or attempt >= self.retries:
                    break
                time.sleep(0.25 * (2 ** attempt))
        raise UniFiApiError(str(last or "request failed"))


@dataclass
class SnapshotStore:
    client: UniFiApiClient
    site_id: Optional[str] = None
    host_id: Optional[str] = None
    poll_interval: int = DEFAULT_POLL_INTERVAL
    snapshot: Dict[str, Any] = field(default_factory=lambda: {"schema": "unifi-gateway/v1", "status": "starting"})
    last_error: Optional[str] = None
    lock: threading.Lock = field(default_factory=threading.Lock)

    def refresh(self) -> Dict[str, Any]:
        try:
            sites = self.client.get("/v1/sites")
            site_rows = _items(sites)
            site = site_rows[0] if site_rows else {}
            site_id = self.site_id or str(_first(site, "id", "siteId", default=""))
            metrics = self.client.get("/v1/isp-metrics/5m", {"duration": "24h"})
            devices = self.client.get("/v1/devices")
            sdwan = self.client.get("/v1/sd-wan-configs")
            sdwan_rows = _items(sdwan)
            sdwan_id = str(_first(sdwan_rows[0], "id", default="")) if sdwan_rows else ""
            sdwan_status = self.client.get(f"/v1/sd-wan-configs/{sdwan_id}/status") if sdwan_id else {}
            clients = None
            client_error = None
            if self.host_id and site_id:
                try:
                    clients = self.client.get(f"/v1/connector/consoles/{self.host_id}/proxy/network/integration/v1/sites/{site_id}/clients")
                except UniFiApiError as exc:
                    client_error = str(exc)
            else:
                client_error = "Connector host_id/site_id not configured"
            data = normalize_snapshot(sites, metrics, devices, sdwan, sdwan_status, clients, client_error=client_error)
            with self.lock:
                self.snapshot = data
                self.last_error = None
            return data
        except UniFiApiError as exc:
            with self.lock:
                self.last_error = str(exc)
                if self.snapshot.get("schema"):
                    self.snapshot = dict(self.snapshot, status="stale", error=str(exc))
            return self.snapshot

    def get(self) -> Dict[str, Any]:
        with self.lock:
            return dict(self.snapshot)

    def loop(self, stop: threading.Event) -> None:
        while not stop.is_set():
            self.refresh()
            stop.wait(self.poll_interval)


def config_from_env() -> SnapshotStore:
    return SnapshotStore(
        UniFiApiClient(os.getenv("UNIFI_API_BASE_URL", DEFAULT_BASE_URL), os.environ["UNIFI_API_KEY"]),
        site_id=os.getenv("UNIFI_SITE_ID") or None,
        host_id=os.getenv("UNIFI_HOST_ID") or None,
        poll_interval=int(os.getenv("UNIFI_POLL_INTERVAL", str(DEFAULT_POLL_INTERVAL))),
    )
