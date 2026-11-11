<p align="right">
  <strong>简体中文</strong> · <a href="unifi-dashboard.md">English</a>
</p>

# UniFi 监控仪表盘

本应用面向 AI Passport 的 ESP32-C3、8 MB Flash、无 PSRAM、240 × 320 屏幕和三枚按键。复用 BSP，采用独立设计的中文界面，参考 UniFi 深色控制台的文字分区、蓝色强调、折线图、堆叠柱状图、环形图和示意拓扑。设备通过 Wi-Fi 发起只读 UniFi API 请求，不需要电脑或 NAS 网关。

## 首次连接与配置保存

1. 新设备缺少 Wi-Fi 或 API Key 时会开启临时设置热点，屏幕显示 `Passport-...` 名称、固定的热点密码 `12345678` 和 `http://192.168.4.1` 地址。之后也可从**设置与配网 → 开启配置热点**进入。
2. 手机连接该热点，打开 `http://192.168.4.1`。手机提示没有互联网时仍保持连接；直接进入配置页面，不再要求额外设置码。
3. 在 Wi-Fi 步骤扫描或输入 **2.4 GHz** SSID，填写密码并保存连接。网页显示连接结果。扫描最多列出八个网络；隐藏或未列出的 SSID 可手动填写。
4. 在 UniFi 步骤选择云端或局域网模式，粘贴对应 API Key，保存并测试。等待网络校时和首次查询，再选择发现的站点，也可手动输入标识。
5. 设置亮度、自动息屏时间、刷新间隔和演示模式，完成后关闭热点。热点也会在十分钟后自动关闭，需要修改配置时再从设备开启。

手机将配置发送到设备临时 HTTP 服务。服务只接受设置 AP 地址上的请求，检查对端网段和 Host/Origin，热点只允许一台手机关联。配置通道依靠热点 WPA2 密码保护，不是普通局域网上持续开放的 HTTPS 管理网站。关闭热点会停止 HTTP 服务并清除临时访问凭据。

API Key、SSID、Wi-Fi 密码、所选站点、可选控制台信任证书及显示设置保存为设备 Flash 中的 `unifi_cfg/config_v1` 版本化 NVS blob。密钥**不写进源代码或分发的固件**。当前版本**未启用 NVS 或 Flash 加密**，通过物理读取 Flash 可能取得已保存凭据。网页只显示是否已配置，不会读回保存的 API Key 或密码。密钥输入框留空表示保留，勾选明确的清除选项才删除；不使用浏览器本地存储副本，不导出凭据文件，也不记录凭据日志。

`CONFIG_UNIFI_DEMO_MODE` 只在没有 NVS 配置时决定初始模式，默认是真实连接/设置模式。真实凭据通过网页输入，不通过 Kconfig。旧版 `unifi/gateway`、`unifi/ssid`、`unifi/password` 配置不会自动迁移。当前配网采用 SoftAP，复用了 BLUFI 参考实现的生命周期处理方式，没有启用 BLE 配网服务。

更换 API Key、切换云端/局域网模式或修改局域网控制台地址后，会清除原站点选择并重新发现。新连接成功后请重新选择站点。密钥输入框留空仍会保留已有凭据；自动选站会按站点身份保持稳定，不随 API 列表顺序变化。

## WAN 自动识别

总览改为读取所选网关的实际 WAN，不再使用 SD-WAN 的备用链路字段。仅显示发现的 `WAN1`、`WAN2` 等线路，分别展示当前互联网状态、独立的历史可用率数字/条形图，以及线路查询时间。超过两条时每五秒轮换一组；**OK → WAN 线路详情**可手动浏览互联网状态、物理链路、历史可用率、API 报告的统计周期和查询时间。区分停用、未知、查询失败以及超过两分钟的过期状态。单 WAN 站点不再出现备用线路占位。内存最多保留八条线路，更多线路明确显示 `8+`/部分结果；成功重新识别后移除已不存在的接口。

沿用只读控制台 `stat/health` 响应获取 `gw_mac` 和各接口 `uptime_stats.WAN/WAN2/...` 下的历史数据（`availability`、`time_period`）。随后通过现有 HTTPS/Connector 路径仅请求 `/api/s/{internalReference}/stat/device/{gatewayMac}`，读取匹配网关的 `wan1/wan2/...`、`last_wan_status` 和 `last_wan_interfaces.*.alive`。互联网状态、物理 `up`、配置 `enable` 及出口 `is_uplink` 分开处理。历史可用率 100% 或物理链路 UP 都不能证明当前互联网连通。查询失败保留上次数据，不更新其时间戳。这些控制台兼容接口依赖实际版本；权限不足或字段缺失明确显示未知/错误，不虚构在线状态。

Network 11.0.97 OpenAPI 中 `/v1/sites/{siteId}/wans` 只有 `id` 和 `name`，不能确定实时状态；不会按返回数组顺序把 WAN UUID 猜测映射到控制台线路编号。固件每轮不额外查询这个名称列表。

增量 JSON 解析器现在对字段路径中的 `/` 和 `~` 进行转义，兼容控制台的软件包/版本字段，同时避免把包含斜杠的字面键误当成嵌套状态路径。真实响应只保存在本地忽略的私有测试文件中，不将密钥或私有网络响应嵌入固件。HTTP/JSON 分块缓冲保持有界，不在内存累计完整响应。持久化配置和分区布局未改动。WAN 按配置的轮询间隔更新，不是推送或秒级连续数据。

## 自动息屏与时钟

默认 60 秒没有按键操作后息屏。手机配置后台的显示步骤可选择 30 秒、1 分钟、2 分钟、5 分钟或始终亮屏；设备设置页也显示当前息屏时间。首次按任意键恢复已保存的亮度，并消耗整次唤醒操作（包括长按 OK），再次按键才执行导航。持续按住按键不会息屏。API 更新不会重置无操作计时。

息屏通过 BSP 将背光亮度设为零，并停止应用的定时重绘；Wi-Fi、API 轮询和数据缓存继续运行。这不是深度休眠，实际续航改善需要测量功耗。无操作计时使用单调时钟，不受网络校时影响。

顶栏在站点/设备名称右侧、现有 Wi-Fi 与电量图标之前显示 24 小时制北京时间（UTC+8）。网络时间尚不可用时显示 `--:--`，不使用 ISP 指标的历史时间。较长名称仍限制在名称区域内。

新增息屏时间单独保存为 NVS `unifi_cfg/screen_idle_s` 无符号整数，原有 2700 字节 `config_v1` 布局保持不变；旧配置缺少新键时默认 60 秒。这保证配置格式兼容，但烧录时保留数据仍需采用[配置教程](../assets/setup.zh_CN.md)中的兼容分段刷写方式。修改超时后的重启持久化及实际电流需要真机验收。

## 云端与局域网模式

| 模式 | 连接与可用内容 | 限制 |
| --- | --- | --- |
| 云端 Site Manager | HTTPS `api.ui.com`、`X-API-KEY`，查询 Site Manager 站点/设备/ISP/SD-WAN，并通过 Connector 代理查询 Network 站点、客户端和设备统计 | 需要 Connector 与 API 权限，受云端可用性和限流影响 |
| 局域网 Network | 设备直连控制台私有 IPv4 HTTPS 地址的 `/proxy/network/integration/v1/...`，使用兼容的 Network API Key | 提供 Network 站点、当前客户端、设备和设备统计；不提供云端 ISP 历史及 Site Manager SD-WAN 数据 |

局域网模式接受私有 IPv4 HTTPS 地址，可带端口。必须通过证书链和主机名/IP 校验；控制台证书不受内置 CA 信任时，需要粘贴合适的 PEM 信任证书。证书没有匹配的 IP 标识时仍会验证失败。没有“跳过 TLS 验证”选项。云端请求使用内置 CA bundle，TLS 请求等待有效的网络时间。

云端模式不会把 Site Manager 的不透明 `siteId` 直接当作 Network UUID。设备先查询 Network `/sites`，将 `internalReference` 与 Site Manager `meta.name` 匹配，也支持显式 UUID 或只有一个站点的结果。局域网模式不需要云端 Host ID 即可发现单个 Network 站点；存在多个无法区分的本地站点时，需明确填写 Network 站点 UUID。当前最多保留八个云端站点，读取最多 25 个 Network 站点映射记录。

早期的 [gateway/ Python 网关](../../gateway/README.zh_CN.md) 保留为旧网关客户端固件和外部工具的可选组件。当前直连 API 固件不读取其 `/api/snapshot`。该目录中“密钥保存在服务器”的说明只适用于早期架构。

## 页面与按键

在主页面，上/下键切换九页；短按 OK 打开当前页菜单，上/下选择，OK 确认。长按 OK 从菜单/详情返回当前主页面，在主页面长按则回到网络总览。刷新是菜单中的独立操作，查看详情不会意外触发刷新。按键仅投递任务，网络请求和 Flash 写入在 UI 任务之外执行。

| 主页面 | 显示内容 | OK 菜单/详情 |
| --- | --- | --- |
| 网络总览 | 自动识别各 WAN 的当前状态、历史可用率、在线设备与客户端数量 | WAN 线路详情；七天流量可用性说明（演示模式保留模拟柱状图）；立即刷新 |
| 互联网 | WAN 当前下载/上传及本机近期采样曲线 | 平均/最大延迟和丢包详情；立即刷新 |
| 客户端 | 在线总数及无线/有线分布，访客、VPN、Teleport 数量 | 浏览当前在线客户端的类型、IP 和连接时长；立即刷新 |
| VPN 连接 | 分别显示 VPN/Teleport 数量，仍在线的最近上线 VPN | 列出当前在线 **VPN 和 Teleport** 的 IP、时长；立即刷新 |
| SD-WAN | 站点连接示意及支持时的已连接/总隧道数 | 主备 WAN 报告、延迟、Hub/Spoke、错误和警告；立即刷新；重新连接能力说明 |
| 设备运行 | 主要设备运行时长、在线/离线分段条及可用的 CPU/内存条 | 选择设备查看状态、运行/启动时间或上联接收发送速率；立即刷新 |
| 告警事件 | 本设备观察到的最近状态变化 | 本次启动最多八条内存事件；立即刷新 |
| 站点切换 | 有限站点列表及统计 | 选择并保存监控站点；立即刷新 |
| 设置与配网 | Wi-Fi、本机 IP、数据模式和配置热点状态 | 开启/关闭热点、切换演示模式、立即刷新 |

客户端详情每屏显示三行，API 页缓存保留六条。名称/IP 按固定容量安全截断，未知值显示不可用，并在需要时注明部分结果或 API 错误。缺失数据不会被解释为零流量或零 VPN。页脚按数据区分别显示时间：当前 WAN、客户端、Mesh 超过两分钟标为过期；云端 ISP 保留实际采样时间，超过一小时标为过期。模拟数据始终标记为演示。低电量来自 BSP 电量读数，不使用虚构估算。

屏幕按已确认的第二版设计组织：顶部站点与状态图标，页面图标/标题与页码，独立的副标题/演示标记，固定内容区，数据年龄及九点导航。图标和图表使用 LVGL 原生线条、圆弧和矩形绘制，不引入 SVG 运行库或全屏图片缓存。离线预览与设备演示共用绘图代码、字体和纯 C 演示快照；演示包含两个虚构站点、92 个分页客户端、三个当前 VPN 客户端及模拟已连接的 Mesh 链路。这些样例不代表 API 已支持或真实网络状态。演示选站仅保存在内存，不替换已保存的真实站点标识。

## API 字段与统计口径

实现依据 [Site Manager 1.0.0](https://developer.ui.com/site-manager/v1.0.0/ai-gettingstarted.md) 和提供的 [Network 11.0.97 规范](https://developer.ui.com/network/v11.0.97/ai-gettingstarted.md)。实际控制台版本、权限和可选字段仍需真机验收。

| 只读接口 | 使用字段/含义 |
| --- | --- |
| Site Manager `/v1/sites` | `meta.desc/name`、`hostId/siteId`、`statistics.counts`、`percentages.wanUptime`、`ispInfo.name/organization`、`internetIssues`。后者保留报告的问题数量。推导的 `devices_online/devices_offline` 状态仅描述设备数量，**不能证明 WAN 健康**；此接口没有规范化的站点健康标志。 |
| `/v1/devices` | 所选 Host 的设备 `name`、`status`、`startupTime`，有限设备清单及运行时长。 |
| `/v1/isp-metrics/5m?duration=24h` | `metricTime`、`wan.avgLatency/maxLatency/packetLoss/download_kbps/upload_kbps`，最近有效样本及 48 个半小时趋势桶。 |
| 控制台 `/api/s/{internalReference}/stat/health` | `wan.rx_bytes-r/tx_bytes-r`，每秒字节数乘八换算当前下载/上传 bits/s。只取 `wan`，不重复累加 `www` 子系统。 |
| 控制台 `/api/s/{internalReference}/stat/remoteuservpn` | `up:true` 当前远程会话的 `id/user/tunnel_ip/vpn_type/uptime/last_seen`，补充 Integration 客户端接口缺少的 VPN/Teleport。 |
| `/v1/sd-wan-configs` 及 `/{id}/status` | 所选站点匹配的 `sdwan-hbsp` 或 `s2s-vpn` 配置。前者使用 Hub/Spoke 契约字段；Mesh 使用 `data.status.connections[].sites[].status.connected/offline`，同时匹配 Host 和 Site，以及站点错误/警告。最多发现八个配置，每轮至多探测两个配置的成员关系，要求 Host/Site 唯一匹配；有歧义、不支持或发现不完整时显示不可用。 |
| Connector `/v1/connector/consoles/{hostId}/proxy/network/integration/v1/sites` | 先通过 UUID 和 `internalReference` 完成映射，再读取 Network 站点内容。 |
| Network `/v1/sites/{siteId}/clients` | `id/name/type/ipAddress/connectedAt/access.type`，识别 `WIRED`、`WIRELESS`、`VPN`、`TELEPORT`；访客是子集，不重复加到总数。 |
| Network `/v1/sites/{siteId}/devices` 及 `/{deviceId}/statistics/latest` | 设备 ID/状态、`uptimeSec`、可选 CPU/内存百分比、`uplink.rxRateBps/txRateBps`，统计按当前所选设备读取。 |

Network `/clients` 返回**当前已连接**客户端，无法从此接口取得控制台历史列表中的离线/已配置 VPN 总数。“最近上线 VPN”跨已扫描的所有分页，选择当前 `VPN` 记录中最大的有效 `connectedAt`，不包含 Teleport。缺失连接时间保持未知，完整详情列表则按类型包含 Teleport。Connector 失败时保留 Site Manager 的 Wi-Fi + 有线回退总数，该数可能不含 VPN，不能伪装成完整在线客户端总数。

当前 WAN 速率来自控制台健康响应。互联网主页面显示当前速率和最多 48 个内存采样点，保留每次查询时间；超过两分钟的空档不连线。这仍是轮询，不是持续推送，查询时间也不冒充上游采样时间。互联网详情保留五分钟粒度的云端 ISP 延迟/丢包，并平均到 48 个半小时桶。缺失桶不补零，始终保留原始 `metricTime`；当前速率与历史指标不互相覆盖新鲜度。

已停用原先根据 ISP `download_kbps/upload_kbps` 积分计算七天流量的逻辑。这些字段不能独立证明实际字节用量。真实模式现在说明七天流量暂不可用，等待接入经过验证的字节统计；演示柱状图仍明确标记为模拟。不会产生虚假的 GB 总量，也不把七天历史写入 NVS。

上述控制台兼容接口通过同一经过认证的 Connector 路径或可信本地控制台访问，**不属于所提供的 Integration v1 OpenAPI 契约**，可能随控制台版本变化。本次已在 Network **10.6.109** 只读验证，HTTP、格式和权限失败会明确提示。只有官方客户端扫描没有 VPN 和 Teleport 时才补查专用会话，避免两个来源重复计数。仅 `up:true` 算在线；上线时间取 `last_seen - uptime`，缺少控制台时间时才回退为查询时间减 uptime，缺少时长保持未知。兼容 VPN 接口 HTTP 403/404 后等待五分钟再试，未确认覆盖时不会显示零在线 VPN。

Network 上联字段名称以 `Bps` 结尾，但所给规范没有独立证明单位及平均周期。应用保留原数值，沿用 API 的 `Bps` 标记并用十进制 K/M 前缀显示，**不会**乘八换算为 Mbps，不把它称作 WAN 下载/上传，也不把收发比例称作带宽占用率。

接口也支持已发布 `sdwan-hbsp` 示例以外的 Mesh（`s2s-vpn`）状态。Mesh 显示已连接/总**链路**数、两端站点名称及当前站点错误/警告；所有端点明确 connected 且非 offline 才算已连接，缺少状态字段保持未知。不虚构 Hub/Spoke 角色或主备 WAN 信息。空 `internetIssues` 仅表示无报告问题，不代表独立链路测试。SD-WAN 协议单独重连/重启仍不可用，菜单解释限制，绝不以重启整台网关代替。全部 UniFi 请求为 GET。

## 资源上限、缓存和异常

- 串行执行 HTTPS，每次一个请求；HTTP 收发缓冲各 1,024 字节，读取块 768 字节。有界流式解析器处理大响应，不把完整 JSON 或 24 小时历史放到栈上。解析器和快照工作存储分配在堆上，TLS 和任务栈另需 RAM。
- 单次响应最多读取 1 MB，标量 token 最多 511 字节，嵌套深度最多 20。过大或损坏的响应明确报错，不越界写入。设备清单保留 12 台，客户端保留六条展示记录和一条最近 VPN。
- 默认按每轮开始时间计算 30 秒间隔（超时轮次结束后至少休息两秒）；后台增加 10 秒和 15 秒选项（校验范围 10–3600 秒）。站点 UUID 映射、控制台名称、SD-WAN 成员关系和 ISP 历史缓存五分钟；WAN 优先于 SD-WAN 和客户端查询，设备维护隔轮执行，为历史查询留出独立轮次。历史失败不覆盖当前速率。保存配置后清除这些调度缓存，但保留仍在生效的 429 冷却。手动刷新遵守缓存和 API 退避。
- 客户端最多扫描三页、每页 100 条，按 ID 防重复。超过 300 客户端、分页期间总数改变或时间预算不足时标记统计不全；此时“最近 VPN”也只是已扫描子集中的最近记录。
- 每次 HTTP 操作超时 6.5 秒，并限制读取响应的时长；刷新轮次达到 35 秒软预算后停止发起新工作，最后一个在途请求可能超过该预算。HTTP 429 设置全局 Retry-After 冷却（数字秒，最多一小时，缺失时 30 秒），不在 UI 中 sleep。
- 尚未收到 HTTP 响应时的连接失败允许重试一次（包括首次 TLS）；HTTP 状态失败、不完整响应和 429 不立即重试。请求失败保留先前成功值，并分别标记数据区错误。权限失败不会清空其他数据。本地模式不尝试云端请求。内存事件是本机观察结果，并非 UniFi 系统日志。

## 字体、验证与真机交接

中文 UI 使用本应用的 Noto Sans CJK 子集，检查字形覆盖与位图，大号数字采用独立字体。绘制裁剪在 240 × 320 画布中，预留页头和页脚。用户自定义客户端/设备名称可能包含字库外字符，仍需真机检查。主机 LVGL 预览使用与固件相同的绘图代码和字体，但不能证明 SPI 屏幕表现或按键操作。

在项目根目录执行应用测试和项目验证：

```bash
python3 tests/test_unifi_api.py
python3 tests/test_unifi_fonts.py
python3 -m unittest discover -s tests -p test_unifi_gateway.py
./tools/validate.sh --static
# 激活 ESP-IDF 5.5.3 后：
./tools/validate.sh --firmware
./tools/validate.sh
```

API 测试使用 fake 响应，覆盖超过 8 KB 的分块 JSON、站点/UUID 映射、指标、VPN/Teleport、设备统计、SD-WAN、TLS 配置、429 冷却与缓存错误降级，不访问真实账号。UI、配置校验和网页门户另有主机测试。固件验证成功后生成并归档可从 `0x0` 刷写的合并固件；刷写前阅读[固件布局与存储数据](engineering/firmware-layout.zh_CN.md#烧录与已存数据)，合并固件的间隙可能重置 NVS。

交付时分别报告 **Build**、**Host tests**、**Device tests** 和 **Unverified**。烧录前询问确认，构建/打包命令不烧录。真机待验项目包括：冷启动无白屏/重启循环、中文字形、图表刷新和三按键菜单、首次 Wi-Fi 配网及 NVS 持久化、HTTPS 证书与校时、云端/本地 API 权限、VPN 分页及最近客户端数量、SD-WAN 不支持/错误状态、七天流量不可用提示、设备统计、低电量、断网与恢复。

## 公开示例与私有数据

公开演示站点、API 测试数据和预览图使用虚构名称与文档示例地址。真实站点名称只从配置后的 API 响应获取，不内置任何个人专用别名。API Key 和 Wi-Fi 密码通过设备配置页填写，不写入源码。账号响应、串口日志、凭据、固件和调试包应保留在被 Git 忽略的本地目录中。仓库截图由演示数据生成。网页烧录和 API Key 获取步骤见[根目录 README](../../README.zh_CN.md)。
