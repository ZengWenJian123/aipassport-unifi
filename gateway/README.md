<p align="right"><a href="README.zh_CN.md">简体中文</a></p>

# UniFi local gateway

This optional legacy service is retained for older gateway-client firmware and
external tools. The [current UniFi Passport firmware](../docs/development/unifi-dashboard.md) connects
directly to UniFi, stores its API key on the device, and does not consume this
gateway's endpoints. You do not need to run this service for the current app.

In the older gateway architecture, this read-only service keeps the UniFi
`X-API-KEY` on a computer, NAS, or DGX Spark; its device client stores only Wi-Fi
and the gateway URL. The service polls every 30 seconds, caches the last good
snapshot, retries HTTP 429, and serves compact JSON:

- `GET /api/health` — service and last API error.
- `GET /api/snapshot` — cached normalized data.
- `GET /api/refresh` — fetch immediately, then return the snapshot.

Run `python -m gateway.server` after copying `.env.example` to `.env` and exporting it (or use your process manager). The default bind is `127.0.0.1`; to let the device reach it, set `GATEWAY_BIND=0.0.0.0`, restrict TCP 8787 in the computer/NAS firewall to the device subnet, and do not expose it to the internet. The gateway performs no UniFi write operation.

`isp_metrics.metric_age_sec` and `metric_time` are shown explicitly because the 5-minute API is not second-level telemetry. Connector permissions are optional: if unavailable, `clients.counts` still reports site totals when available and `clients.error` explains why the list is missing.

The endpoint paths and `X-API-KEY` authentication follow the [Site Manager API onboarding](https://developer.ui.com/site-manager/v1.0.0/ai-gettingstarted.md), [Site Manager reference](https://developer.ui.com/site-manager/v1.0.0/llms.txt), [Network API onboarding](https://developer.ui.com/network/v11.0.97/ai-gettingstarted.md), and [Network reference](https://developer.ui.com/network/v11.0.97/llms.txt).
