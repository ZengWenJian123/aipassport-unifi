<p align="right">
  <a href="unifi-dashboard.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# UniFi dashboard

This application runs on AI Passport's ESP32-C3, 8 MB Flash, no PSRAM, 240 × 320 display and three buttons. It reuses the BSP and provides an independent Chinese interface inspired by UniFi's dark console: separated text rows, blue accents, line charts, stacked bars, rings and a symbolic topology. It performs read-only UniFi API requests over Wi-Fi; a computer or NAS gateway is not required.

## First connection and saved configuration

1. On a new device, the temporary setup hotspot starts when Wi-Fi or the API key is missing. The screen shows its `Passport-...` name, the fixed hotspot password `12345678`, and the address `http://192.168.4.1`. It can also be opened from **Settings → Open setup hotspot**.
2. Connect a phone to that hotspot and open `http://192.168.4.1`. Keep the phone connected even if it reports that this network has no Internet access. The configuration page opens directly; no additional setup code is required.
3. In the Wi-Fi step, scan or enter a **2.4 GHz** SSID, enter the password, then save and connect. The page shows connection results. Scan results are limited to eight networks; a hidden or omitted SSID can be entered manually.
4. In the UniFi step, select cloud or LAN mode, paste the appropriate API key, and save/test. Allow time for network clock synchronization and the initial API read. Select a discovered site, or enter its identifiers manually.
5. Set brightness, screen-off timeout, refresh interval and demo mode, then finish and close the hotspot. It also closes automatically after ten minutes. Reopen it from the device when configuration needs changing.

The phone sends configuration to the device's temporary HTTP server. The server accepts requests through its setup AP address, checks the peer subnet and Host/Origin, and allows one associated phone. This is a local configuration channel protected by the hotspot's WPA2 password; it is not an HTTPS administration website available on the normal LAN. Closing it removes the HTTP service and clears the temporary access credentials.

The API key, SSID, Wi-Fi password, selected site, optional console trust certificate and display settings are saved as `unifi_cfg/config_v1`, a versioned NVS blob in the device's Flash. The key is **not compiled into source code or the distributed firmware**. This version does **not enable NVS or Flash encryption**: physical Flash extraction can reveal saved credentials. The page only reports whether a secret is configured; it never reads the saved API key or password back. Leaving a secret field blank preserves it; explicit clear controls remove it. There is no browser local-storage copy, exported credential file or credential logging.

`CONFIG_UNIFI_DEMO_MODE` selects the initial mode only when no saved NVS configuration exists; its default is real/setup mode. Real credentials are entered through the portal, not Kconfig. Existing settings from the older `unifi/gateway`, `unifi/ssid` and `unifi/password` scheme are not migrated. The current configuration workflow uses SoftAP and reuses the lifecycle patterns from the BLUFI reference; a BLE provisioning service is not included.

Changing the API key, switching cloud/LAN mode, or changing the LAN endpoint clears the previous site selection and starts discovery again. Select a site after the new connection succeeds. Blank secret fields retain existing credentials. Automatic site selection remains tied to site identity when API ordering changes.

## Automatic WAN discovery

Overview now discovers WAN interfaces from the selected gateway instead of using SD-WAN's secondary-link field. It shows only discovered `WAN1`, `WAN2`, etc., with current Internet state, a separate historical availability percentage/bar, and an independent link-update age. More than two interfaces rotate in pairs every five seconds; **OK → WAN details** allows manual browsing of current Internet state, physical carrier, availability, the API-reported statistics period and query age. Disabled, unknown, failed and older-than-two-minute samples are distinguished. A single-WAN site has no placeholder backup row. Up to eight WAN slots are retained; additional slots produce an explicit `8+`/partial indicator. A successful re-discovery removes interfaces no longer present.

The existing read-only controller `stat/health` response supplies `gw_mac` and per-interface `uptime_stats.WAN/WAN2/...` history (`availability`, `time_period`). The firmware then requests only `/api/s/{internalReference}/stat/device/{gatewayMac}` via the existing HTTPS/Connector path. It accepts the matching gateway's `wan1/wan2/...`, `last_wan_status` and `last_wan_interfaces.*.alive`. Internet status, physical `up`, configured `enable` and active `is_uplink` are separate fields. Neither a 100% historical value nor carrier UP is treated as proof of current Internet connectivity. Failed requests retain the previous sample without advancing its timestamp. These controller compatibility endpoints are version-dependent; missing permission/schema remains an explicit unknown/error, not an invented online result.

The supplied Network 11.0.97 OpenAPI lists `/v1/sites/{siteId}/wans`, but that schema exposes only `id` and `name`; it cannot establish live status. Its list order is not used to join arbitrary WAN UUIDs to controller WAN numbers. No additional WAN-list request is needed in each firmware refresh.

The incremental JSON reader now escapes `/` and `~` in object-key path segments. This accepts the controller's package/version keys without confusing a literal slash key with a nested status path. Raw responses stay in ignored private local test files; no key or private network response is embedded in the firmware. HTTP/JSON buffers remain bounded; responses are not accumulated in RAM. Persistent configuration and partition layout are unchanged. WAN data follow the configured polling interval, not a push/second-by-second feed.

## Automatic screen off and clock

The display turns off after 60 seconds without a button action by default. In the phone configuration page's display step, select 30 seconds, 1 minute, 2 minutes, 5 minutes, or always on. The device settings page also shows the selected timeout. The first press of any button wakes the screen at the saved brightness and consumes that gesture, including a long OK press; press again to navigate. Holding a button prevents timeout. API updates do not reset the inactivity timer.

Screen off sets BSP backlight brightness to zero and stops the application's periodic redraw. Wi-Fi, API polling, and cached data updates continue. This is not deep sleep; battery-life improvement requires a physical power measurement. The idle timer uses monotonic time, so network clock corrections do not affect it.

The top bar shows the current 24-hour Beijing time (UTC+8) to the right of the site/device name, before the existing Wi-Fi and battery indicators. Before network time is available it shows `--:--`. It does not use the timestamp of the ISP data. Long names remain clipped to their own area.

The new timeout is stored separately as the NVS `unifi_cfg/screen_idle_s` unsigned integer. The existing 2700-byte `config_v1` layout is unchanged; an older configuration without the new key defaults to 60 seconds. This preserves configuration compatibility, but preserving data during flashing still requires the compatible segmented workflow in the [setup guide](../assets/setup.md). Changed timeout persistence across reboot and current consumption require on-device acceptance checks.

## Cloud and LAN modes

| Mode | Connection and available information | Limits |
| --- | --- | --- |
| Cloud Site Manager | HTTPS `api.ui.com`, `X-API-KEY`, Site Manager sites/devices/ISP/SD-WAN plus the Connector proxy to Network sites, clients and device statistics | Connector and API permissions are required; cloud availability and rate limits apply |
| LAN Network | Device connects directly to the console's private IPv4 HTTPS address and `/proxy/network/integration/v1/...` using a compatible Network API key | Network sites, current clients, devices and device statistics; no cloud ISP history or Site Manager SD-WAN data |

LAN mode accepts a private IPv4 HTTPS address, optionally with a port. It requires a valid certificate chain and hostname/IP verification; paste the console's appropriate PEM trust anchor if it is not trusted by the built-in CA bundle. A certificate without a matching IP identity will still fail validation. There is no “skip TLS verification” option. Cloud calls use the built-in CA bundle. TLS requests wait for a valid network clock.

For cloud mode, the Site Manager opaque `siteId` is not reused as a Network UUID. The device queries Network `/sites` and matches `internalReference` to Site Manager `meta.name`; an explicit UUID or a single-site result can also be selected. LAN mode discovers a single Network site without needing a cloud Host ID. Multiple ambiguous LAN sites require an explicit Network site UUID. This version supports up to eight cloud sites and reads up to 25 Network site mapping records.

The earlier Python gateway under [gateway/](../../gateway/README.md) remains an optional legacy component for older gateway-client firmware and external tools. The current direct-API firmware does not consume its `/api/snapshot` endpoint. Its server-side key storage instructions apply only to that earlier architecture.

## Pages and buttons

On a main page, Up/Down switches between nine pages. A short OK press opens that page's menu; Up/Down selects an item and OK confirms. Long OK returns from a menu/detail to its main page, or from a main page to Overview. Refresh is a menu action, so selecting details does not unexpectedly refresh. Buttons enqueue work; network requests and Flash writes run outside the UI task.

| Main page | Display | OK menu/details |
| --- | --- | --- |
| Overview | Automatically discovered WAN states and historical availability, online device and client counts | WAN details; seven-day traffic availability explanation (synthetic bars in demo only); refresh |
| Internet | Current WAN download/upload and recent local sampling trend | Average/max latency and loss details; refresh |
| Clients | Online total and wireless/wired distribution; guest, VPN and Teleport counts | Scroll current online clients, their type, IP and connection duration; refresh |
| VPN | Separate VPN/Teleport counts; most recently connected VPN that is still online | List current VPN **and Teleport** clients with IP and duration; refresh |
| SD-WAN | Symbolic peer link and connected/total tunnels when supported | Primary/backup WAN reports, latency, Hub/Spoke and errors/warnings; refresh; reconnection availability explanation |
| Devices | Primary device uptime, online/offline segments and available CPU/memory bars | Select device for state, uptime/start time, or uplink receive/transmit rates; refresh |
| Events | Recent changes observed by this device | At most eight in-memory events from this boot; refresh |
| Sites | Bounded site list and counts | Select a site and save the selection; refresh |
| Settings | Wi-Fi, device IP, data mode and setup hotspot status | Open/close hotspot, switch demo mode, refresh |

A client detail screen shows three rows at a time; the retained API page contains six records. Client names/IPs are truncated safely at their fixed limits. Unknown values display as unavailable, with partial-result or API-error status where applicable. Missing data is not interpreted as zero traffic or zero VPN connections. The footer uses section-specific data age: current WAN/client/Mesh values become stale after two minutes; cloud ISP samples retain their actual timestamps and become stale after one hour. Demo data is explicitly labelled. Low battery uses the BSP reading, not an invented estimate.

The display follows the approved second design: site and status icons at the top, a page icon/title and page number, a separate subtitle/demo badge, a bounded content area, data age and nine navigation dots. Icons and charts use native LVGL lines, arcs and rectangles; there is no SVG runtime or full-screen bitmap allocation. The same drawing code, fonts and pure-C demo snapshot generate the offline preview and device demo. The demo includes two synthetic sites, 92 paginated clients, three current VPN clients and a simulated connected Mesh link. These samples do not establish API support or live network state. Demo site selection stays in RAM and does not replace saved real site identifiers.

## API fields and interpretation

The implementation follows [Site Manager 1.0.0](https://developer.ui.com/site-manager/v1.0.0/ai-gettingstarted.md) and the supplied [Network 11.0.97 specification](https://developer.ui.com/network/v11.0.97/ai-gettingstarted.md). Actual console version, permissions and optional fields still need device acceptance testing.

| Read-only endpoint | Fields consumed / interpretation |
| --- | --- |
| Site Manager `/v1/sites` | `meta.desc/name`, `hostId/siteId`, `statistics.counts`, `percentages.wanUptime`, `ispInfo.name/organization`, `internetIssues`. The latter is a reported issue count. The derived `devices_online/devices_offline` status describes device counts, **not proof of WAN health**; the API does not provide a canonical site health flag here. |
| `/v1/devices` | Selected host's device `name`, `status`, `startupTime`; bounded device inventory and elapsed uptime. |
| `/v1/isp-metrics/5m?duration=24h` | `metricTime`, `wan.avgLatency/maxLatency/packetLoss/download_kbps/upload_kbps`; latest valid sample plus 48 half-hour trend bins. |
| Controller `/api/s/{internalReference}/stat/health` | `wan.rx_bytes-r/tx_bytes-r` in bytes/second × 8 for current download/upload bits/second. Use only the `wan` subsystem, not the duplicate `www` values. |
| Controller `/api/s/{internalReference}/stat/remoteuservpn` | Current remote sessions with `up:true`, `id/user/tunnel_ip/vpn_type/uptime/last_seen`; compatibility supplement when Integration clients omit VPN/Teleport. |
| `/v1/sd-wan-configs` and `/{id}/status` | The matching `sdwan-hbsp` or `s2s-vpn` configuration. Hub/Spoke uses its documented fields; Mesh uses `data.status.connections[].sites[].status.connected/offline`, matched by both host and site, plus site errors/warnings. Up to eight configurations are discovered, with at most two membership probes per cycle. A unique match by host and site is required; ambiguous, unsupported or incomplete discovery stays unavailable. |
| Connector `/v1/connector/consoles/{hostId}/proxy/network/integration/v1/sites` | UUID and `internalReference` mapping before per-site Network reads. |
| Network `/v1/sites/{siteId}/clients` | `id/name/type/ipAddress/connectedAt/access.type`; `WIRED`, `WIRELESS`, `VPN`, `TELEPORT`. Guest is a subset, not added again to the total. |
| Network `/v1/sites/{siteId}/devices` and `/{deviceId}/statistics/latest` | Inventory IDs/state, `uptimeSec`, optional CPU/memory percentages, and `uplink.rxRateBps/txRateBps`. Statistics are fetched for the selected device. |

Network `/clients` describes **currently connected** clients. An offline/configured VPN count such as a console's historical list is not available from this endpoint. “Most recent VPN” selects the largest valid `connectedAt` among current `VPN` records, across all scanned pages; it excludes Teleport. Missing connection timestamps remain unknown. The full detail list includes Teleport separately by type. If Connector fails, the device preserves Site Manager's Wi-Fi + wired fallback total, which can exclude VPN, rather than inventing a complete connected-client total.

Current WAN rates come from the controller health response. The Internet page displays those rates and at most 48 recent readings in RAM with their actual query times; gaps over two minutes break the line. This is polling, not a continuous stream. A query time is not claimed as an upstream sampling timestamp. The Internet detail page retains five-minute cloud ISP latency/loss measurements averaged into 48 half-hour bins. Missing samples remain undefined, and the original `metricTime` stays visible. Live rates and cloud history never overwrite each other's freshness.

The old seven-day integration of ISP `download_kbps/upload_kbps` has been disabled. These fields do not independently establish actual byte usage. Real mode now explains that seven-day traffic is unavailable until a verified byte-counter source is implemented. Demo bars remain explicitly synthetic. No fabricated GB totals or seven-day history are written to NVS.

The controller compatibility paths above work through the same authenticated Connector base, or a trusted local console. They are **not part of the supplied Integration v1 OpenAPI contract** and can change with controller versions. Compatibility has been checked with Network **10.6.109**; HTTP/format/permission errors remain explicit. The VPN supplement is used only when the official client scan contains neither VPN nor Teleport, so overlapping sources are not double-counted. Only `up:true` sessions count. Session start is `last_seen - uptime`, falling back to query time minus uptime only when the controller timestamp is absent; missing duration stays unknown. HTTP 403/404 disables compatibility VPN retries for five minutes, and unknown coverage is not displayed as zero online VPNs.

The Network uplink field names end in `Bps`, but the supplied schema does not independently establish their unit or averaging period. The application preserves their numeric values and displays the original API `Bps` notation with decimal K/M prefixes. It does **not** multiply by eight, label these rates as WAN download/upload, or call the receive/transmit proportion a bandwidth utilization percentage.

Mesh (`s2s-vpn`) status extends beyond the published `sdwan-hbsp` example. Mesh displays connected/total **links**, participating site names and local errors/warnings. A connection is healthy only when all endpoints explicitly report connected and not offline; missing flags remain unknown. Mesh does not invent Hub/Spoke roles or primary/backup WAN information. Empty `internetIssues` means no reported issues, not an independent link test. Protocol-only SD-WAN reconnect/restart is still unavailable; the menu explains this and never substitutes a gateway reboot. All UniFi calls use GET.

## Resource bounds, caching and errors

- One serial HTTPS request at a time; 1,024-byte HTTP RX/TX buffers and a 768-byte read chunk. A bounded streaming parser consumes larger responses without storing full JSON or 24-hour history on the stack. Working parser/snapshot storage is allocated on the heap. TLS and task stacks need additional RAM.
- Maximum body consumed: 1 MB per request; scalar token 511 bytes; nesting depth 20. Oversized/malformed responses fail explicitly rather than overrunning memory. Inventory retains 12 devices, six displayed client records and one latest VPN record.
- Default polling is 30 seconds measured from the start of a cycle (at least two seconds idle after an overrun); the portal also offers 10 and 15 seconds (validated range 10–3600 seconds). Site UUID mapping, console names, SD-WAN membership and ISP history have five-minute caches. WAN is queried before SD-WAN and clients; device maintenance runs on alternating cycles, allowing history a separate opportunity. History failures cannot replace current rates. Changing saved configuration clears these cadence caches while preserving an active 429 cooldown. Manual refresh respects cache and API backoff.
- A client scan examines at most three pages of 100 records, with an ID duplicate guard. A scan exceeding 300 clients, changing pagination or its time budget is marked partial; latest VPN is then only the latest within the scanned subset.
- Each HTTP operation has a 6.5-second timeout and a bounded body-read duration. New work stops after a soft 35-second cycle budget, which may be exceeded by the final in-progress request. HTTP 429 establishes a global Retry-After cooldown (numeric seconds, capped at one hour; 30-second fallback) without sleeping in the UI.
- A connection failure before any HTTP response can retry once (including cold TLS); status errors, partial bodies and 429 are not retried immediately. Failed requests retain prior successful values with explicit section errors. Permission failures do not erase other sections. LAN mode does not attempt cloud requests. In-memory event records are local observations, not the UniFi system log.

## Fonts, validation and device handoff

The Chinese UI uses the application's Noto Sans CJK subset assets with glyph coverage and bitmap checks; large numeric text has its own font. Drawing is clipped within a 240 × 320 canvas with a reserved header/footer. Arbitrary user-supplied client/device names can contain characters outside the bundled subset and still require real-device inspection. Host LVGL previews use the same drawing code and font assets; they do not prove SPI display behavior or button operation.

Run the application tests and repository gates from the project root:

```bash
python3 tests/test_unifi_api.py
python3 tests/test_unifi_fonts.py
python3 -m unittest discover -s tests -p test_unifi_gateway.py
./tools/validate.sh --static
# With ESP-IDF 5.5.3 activated:
./tools/validate.sh --firmware
./tools/validate.sh
```

API tests use fake responses, including fragmented JSON larger than 8 KB, site/UUID mapping, metrics, VPN/Teleport, device statistics, SD-WAN, TLS configuration, 429 cooldown and cached error fallback. They never call a real account. UI, configuration-validation and portal tests are separate host checks. A successful firmware gate verifies and archives a merged image for offset `0x0`; consult [firmware layout and stored data](engineering/firmware-layout.md#flashing-and-stored-data) before flashing because merged-image gaps can reset NVS.

Report **Build**, **Host tests**, **Device tests** and **Unverified** separately. Ask before flashing; build and packaging commands do not flash. Remaining hardware checks include cold boot without white-screen/reboot loops, all Chinese glyphs, chart redraw and three-button menus, first-run Wi-Fi provisioning and NVS persistence, HTTPS certificate/time handling, cloud/LAN API permissions, VPN pagination/latest-client counts, SD-WAN unsupported/error states, unavailable seven-day totals, device statistics, low battery and disconnect/reconnect recovery.

## Public examples and private data

Public demo sites, API fixtures and preview images use fictional labels and documentation addresses. Real site names are displayed only from the configured API response; there are no owner-specific display aliases. API keys and Wi-Fi passwords belong in device setup, not source files. Keep account responses, serial logs, credentials and firmware/debug bundles in ignored local storage. The included screenshot is generated from demo fixtures. See the [root README](../../README.md) for browser flashing and API-key setup.
