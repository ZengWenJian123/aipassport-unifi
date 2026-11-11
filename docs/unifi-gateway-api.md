<p align="right"><a href="unifi-gateway-api.zh_CN.md">简体中文</a></p>

# UniFi gateway API contract

This contract describes the optional [legacy gateway](../gateway/README.md),
not the current direct-API firmware. The local gateway exposes only read-only
endpoints and limits `clients.items` to 12 entries. The older device-client
design used a 4–8 KB response buffer.

`GET /api/health` returns `{ "ok": true, "last_error": null, "poll_interval_sec": 30 }`.

`GET /api/snapshot` and `GET /api/refresh` return:

```json
{"schema":"unifi-gateway/v1","generated_at":"2026-11-07T01:02:03Z","site":{"id":"s1","name":"Home","status":"online","wan_uptime_sec":86400,"internet_issues":[],"isp":{"name":"Acme","status":"-"},"online_devices":3,"online_clients":7},"isp_metrics":{"average_latency_ms":12.5,"max_latency_ms":44,"packet_loss_pct":0.3,"upload_bps":100,"download_bps":1000,"uptime_sec":0,"metric_time":"2026-11-07T01:00:00Z","metric_age_sec":120},"devices":{"total":4,"online":3,"offline":1,"offline_names":["ap-2"],"startup_times":[{"name":"gw","startup_time":"2026-11-06T00:00:00Z"}]},"sd_wan":{"connected_tunnels":1,"total_tunnels":3,"hub_count":1,"spoke_count":2,"primary_wan":"fiber","backup_wan":"lte","latency_ms":21,"errors":[],"warnings":[]},"clients":{"counts":{"total":4,"wireless":1,"wired":0,"guest":1,"vpn":1,"teleport":1},"items":[{"name":"phone","type":"WIRELESS","ip":"10.0.0.2","connected_at":"2026-11-07T01:00:00Z"}],"error":null}}
```

The upstream `/v1/isp-metrics/5m?duration=24h` data is five-minute telemetry; `metric_age_sec` and `metric_time` are therefore displayed as data freshness. Connector failures do not discard site counts and are surfaced in `clients.error`. `status: "stale"` and `error` indicate that the last refresh failed and the cached snapshot is being served.
