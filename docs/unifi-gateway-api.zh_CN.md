<p align="right"><a href="unifi-gateway-api.md">English</a></p>

# UniFi 网关 API 契约

本文描述可选的[旧版网关](../gateway/README.zh_CN.md)，不适用于当前直连 API
固件。本地网关只提供只读接口，`clients.items` 最多返回 12 条；旧版设备客户端
设计使用 4–8 KB 的 HTTP JSON 响应缓冲区。

`GET /api/health` 返回服务状态、最近错误和轮询间隔。`GET /api/snapshot` 与 `GET /api/refresh` 返回站点状态、WAN uptime、ISP 5 分钟指标、设备在线/离线、SD-WAN 隧道、客户端计数和最多 12 条客户端摘要。字段结构与英文示例相同。

`/v1/isp-metrics/5m?duration=24h` 是 5 分钟粒度数据，`metric_age_sec` 与 `metric_time` 用于显示数据时间，不能伪装成秒级实时。Connector 失败不会丢弃站点数量，并在 `clients.error` 中报告。刷新失败时服务继续返回上一次缓存，并设置 `status: "stale"` 与 `error`。
