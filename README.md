<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# UniFi Passport

A pocket UniFi network dashboard for **FoloToy AI Passport**, with a Chinese
interface, a 240 × 320 display and three physical buttons. The ESP32-C3 queries
UniFi directly over HTTPS; no always-on computer or NAS gateway is required.

**Use [`feature/unifi-dashboard`](https://github.com/ZengWenJian123/aipassport-unifi/tree/feature/unifi-dashboard)
for the application source and full instructions.** This `main` branch keeps
the upstream AI Passport firmware baseline plus this project index. Building
`main` builds the baseline hardware demo, not the UniFi application.

## UniFi dashboard application

![UniFi Passport demo screens](docs/assets/unifi-dashboard-preview.png)

Offline LVGL renders using synthetic demo data; these are not device photographs
or live network readings.

The application provides nine pages: Overview, Internet, Clients, VPN, SD-WAN,
Devices, Events, Sites and Settings. Use Up/Down to navigate, short OK to open
menus or confirm, and long OK to return. It includes:

- Current WAN download/upload polling and trends, plus cloud ISP latency/loss.
- Connected clients, VPN/Teleport sessions, device uptime and available statistics.
- Cloud Site Manager or direct LAN Network API access and saved site selection.
- Phone configuration through a temporary Wi-Fi hotspot, without putting keys in source.
- Brightness, refresh interval, automatic screen off, Beijing time and offline demo mode.
- Read-only API access and explicit missing, stale, partial and error states.

Actual data depend on console versions and API permissions. Real seven-day
traffic totals are unavailable; demo bars are synthetic. Screen off is not deep
sleep, and UniFi configuration writes or reboot commands are not implemented.

## Get the application

```bash
git clone --branch feature/unifi-dashboard https://github.com/ZengWenJian123/aipassport-unifi.git
cd aipassport-unifi
```

Hardware: FoloToy AI Passport, ESP32-C3, 8 MB Flash, no PSRAM, and 2.4 GHz Wi-Fi.
Build with **ESP-IDF 5.5.3**. The application branch contains the full source,
Chinese font assets, tests and setup portal.

| Guide | Contents |
| --- | --- |
| [Application README](https://github.com/ZengWenJian123/aipassport-unifi/blob/feature/unifi-dashboard/README.md) | Setup, nine pages, buttons, build and flashing commands, limits |
| [Application design](https://github.com/ZengWenJian123/aipassport-unifi/blob/feature/unifi-dashboard/docs/development/unifi-dashboard.md) | API semantics, resource bounds and recorded validation |
| [Upstream platform docs](docs/README.md) | Hardware, BSP, environment and development rules |
| [Agent instructions](AGENTS.md) | Required repository workflows for AI-assisted changes |

First-time setup uses the `Passport-...` hotspot shown on the device, password
`12345678`, and <http://192.168.4.1>. Enter Wi-Fi and the appropriate API key,
select a site, then close the hotspot. See the application README for LAN TLS
requirements, credential storage and data-preserving firmware updates. A merged
flash can reset saved settings; a successful build is not hardware acceptance.

## Branches and credits

| Branch | Role |
| --- | --- |
| `main` | Upstream firmware baseline with the fork's README and documentation assets |
| `feature/unifi-dashboard` | Complete UniFi application and its documentation |

Based on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), preserving
upstream history and the [MIT License](LICENSE). The application branch's Noto
Sans CJK font assets are distributed under SIL Open Font License 1.1.
