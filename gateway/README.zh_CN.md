<p align="right"><a href="README.md">English</a></p>

# UniFi 本地网关

此服务作为可选的旧版组件保留，供旧版网关客户端固件和外部工具使用。
[当前 UniFi Passport 固件](../docs/development/unifi-dashboard.zh_CN.md)直接连接 UniFi，API Key 保存在
设备上，不会调用此网关的接口。使用当前应用无需运行该服务。

在旧版网关架构中，这个只读服务把 UniFi `X-API-KEY` 保存在电脑、NAS 或
DGX Spark，设备客户端只保存 Wi-Fi 配置和网关地址。服务默认每 30 秒轮询，
缓存最近一次成功结果，遇到 HTTP 429 会重试，并提供紧凑 JSON：

- `GET /api/health`：服务状态和最近错误。
- `GET /api/snapshot`：缓存的归一化快照。
- `GET /api/refresh`：立即拉取并返回快照。

复制 `.env.example` 后导出环境变量，再运行 `python -m gateway.server`。默认只监听 `127.0.0.1`；设备需要访问时才设置 `GATEWAY_BIND=0.0.0.0`，并在电脑/NAS 防火墙中只放行设备所在网段到 TCP 8787，禁止暴露到互联网。网关不会执行 UniFi 写操作。

`isp_metrics.metric_age_sec` 和 `metric_time` 会明确显示数据新鲜度，因为 5 分钟指标不是秒级遥测。Connector 权限不可用时仍保留站点客户端总数，并在 `clients.error` 中说明原因。

接口路径和认证遵循 [Site Manager API 指南](https://developer.ui.com/site-manager/v1.0.0/ai-gettingstarted.md)、[Site Manager 参考](https://developer.ui.com/site-manager/v1.0.0/llms.txt)、[Network API 指南](https://developer.ui.com/network/v11.0.97/ai-gettingstarted.md) 与 [Network 参考](https://developer.ui.com/network/v11.0.97/llms.txt)。
