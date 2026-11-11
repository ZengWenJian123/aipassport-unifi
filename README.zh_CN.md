<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# UniFi Passport

把 **FoloToy AI Passport** 变成随身携带的 UniFi 网络仪表盘。在 240 × 320
屏幕上，通过三个实体按键和中文界面查看 WAN 流量、在线客户端、VPN 会话、
设备与站点状态。

ESP32-C3 通过 HTTPS 直接连接 UniFi。手机连接设备的临时设置热点，即可配置
Wi-Fi 和 API Key，无需电脑、NAS 或 Python 网关常驻运行。

[快速开始](#快速开始) · [页面与按键](#页面与按键) ·
[编译](#编译与验证) · [应用详细说明](docs/development/unifi-dashboard.zh_CN.md)

## 界面预览

![由 LVGL 渲染的 UniFi Passport 九个页面](docs/assets/unifi-dashboard-preview.png)

图片由固件使用的 LVGL 绘制代码和内置字体离线生成，数据为明确标记的模拟
演示数据，并非设备实拍或真实网络读数。设备界面为中文，文档提供中英文版本。

## 主要功能

- 九个页面：总览、互联网、客户端、VPN、SD-WAN、设备、事件、站点、设置，
  配有操作菜单和详情页。
- 轮询当前 WAN 下载/上传速率并展示近期趋势；云端 ISP 延迟和丢包历史保留
  各自的采样时间。
- 查看无线/有线客户端分布、当前 VPN/Teleport 会话、设备运行时间，以及
  API 提供的 CPU、内存和上联统计。
- 支持云端 Site Manager 和局域网 Network API，自动发现站点并保存选择。
- 手机配置 Wi-Fi、API 凭据、亮度、刷新间隔、自动熄屏和演示模式。
- 显示北京时间（UTC+8）、Wi-Fi 和电量；熄屏后的唤醒按键不误切换页面。
- UniFi 请求全部只读；明确显示数据过期、部分结果和错误；无 UniFi 账号也可
  使用离线演示模式。

## 使用条件

| 项目 | 要求 |
| --- | --- |
| 硬件 | FoloToy AI Passport：ESP32-C3、8 MB Flash、无 PSRAM |
| 交互 | 240 × 320 竖屏，上 / 下 / 确定三个实体按键 |
| 网络 | 可访问所选 UniFi 接口的 2.4 GHz Wi-Fi |
| 云端模式 | Site Manager API Key；Network 详情还需要 Connector/API 权限 |
| 局域网模式 | 兼容的 Network API Key，以及可访问的私有 IPv4 HTTPS 控制台地址 |
| 源码编译 | ESP-IDF **5.5.3**、Python 3 和主机 C 编译器 |
| 烧录 | 支持数据传输的 USB 线和设备串口 |

硬件定义以 [BSP 引脚文件](components/bsp/include/bsp_pins.h) 为准。
可用字段取决于控制台版本和 API 权限。

## 快速开始

1. 按下方说明编译并安装固件。
2. 连接未配置设备屏幕上显示的 `Passport-...` Wi-Fi 热点，当前热点密码为
   **`12345678`**。
3. 在手机浏览器打开 **<http://192.168.4.1>**。提示网络无法访问互联网时，
   仍保持连接；无需额外设置码。
4. 扫描或手动输入你的 **2.4 GHz** Wi-Fi，保存密码，等待设备连接。
5. 选择**云端**或**局域网**模式，填写对应 API Key，保存并测试。
   等待网络校时和站点发现，然后选择需要监控的站点。
6. 设置亮度、刷新间隔和熄屏时间，完成后关闭热点。之后可从设备的
   **设置与配网 → 开启配置热点**重新进入。

设置热点只允许一部手机连接，十分钟后自动关闭。已有密钥输入框留空会保留
原值；需要删除时使用明确的清除选项。凭据和设置保存在设备 NVS，不会编译进
固件，配置页面也不会把已保存的密钥读回浏览器。

局域网模式要求证书链有效，且证书身份与控制台 IP 匹配；需要时在配置页填写
合适的 PEM 信任证书，不能跳过 TLS 校验。云端使用内置 CA 证书包，两种模式
均需要有效的网络时间。

配置页面通过临时 WPA2 热点内的 HTTP 访问。当前版本未启用 Flash/NVS 加密，
物理读取 Flash 可能获得凭据。不要把个人 API Key、Wi-Fi 密码或设备日志提交
到 Git；可选网关的 `.env.example` 仅包含占位配置。

## 页面与按键

| 页面 | 展示内容 |
| --- | --- |
| 总览 | 接口报告的互联网/WAN 可用性、设备和客户端数量 |
| 互联网 | 当前 WAN 速率、近期采样、云端延迟和丢包详情 |
| 客户端 | 在线总数、无线/有线分布、分页客户端列表 |
| VPN | 当前 VPN/Teleport 会话、IP 和连接时长 |
| SD-WAN | 可用的 Mesh/隧道状态、对端信息和接口报告的问题 |
| 设备 | 在线/离线状态、运行时间和可用的设备统计 |
| 事件 | 本次启动期间本机观察到的最多八条变化 |
| 站点 | 已发现的站点及站点选择保存 |
| 设置 | Wi-Fi/IP、数据模式、设置热点和显示设置 |

| 按键 | 操作 |
| --- | --- |
| 上 / 下 | 切换主页面，或移动当前菜单/列表中的选项 |
| 短按确定 | 打开页面菜单，或确认选项 |
| 长按确定 | 从菜单/详情返回；在主页面则回到总览 |
| 熄屏后按任意键 | 仅唤醒，再按一次才执行导航 |

默认 **60 秒**无按键操作后熄屏，可选 30 秒、1 分钟、2 分钟、5 分钟或常亮。
熄屏后 Wi-Fi 与 API 轮询继续运行。默认刷新间隔为 **30 秒**，实际新数据到达
时间还受请求耗时、缓存和 API 退避影响。

## 编译与验证

```bash
git clone https://github.com/ZengWenJian123/aipassport-unifi.git
cd aipassport-unifi
```

按照[环境搭建指南](docs/development/engineering/environment-setup.zh_CN.md)安装并
激活 ESP-IDF 5.5.3，然后在已激活的 ESP-IDF 终端运行：

```bash
idf.py --version             # ESP-IDF v5.5.3
./tools/validate.sh --static  # 仓库检查和主机测试
./tools/validate.sh          # 完整检查，包含固件编译
```

完整检查会在隔离目录中根据 `sdkconfig.defaults` 编译，验证 Flash 布局，并保留：

```text
build/FoloToy-AI-Passport-full.bin  验证后的合并镜像，烧录地址为 0x0
build/firmware/<sha256>/           配套镜像、ELF、MAP 和清单
```

`./tools/validate.sh --firmware` 仅运行固件部分。构建产物不纳入 Git。
仓库已包含生成好的中文字库 C 源码，普通编译无需重新生成字体。

对于空白设备或有意完整刷新固件的情况，把 `PORT` 替换成实际串口，烧录验证后
的合并镜像：

```bash
python -m esptool --chip esp32c3 --port PORT write-flash \
  0x0 build/FoloToy-AI-Passport-full.bin
```

**合并镜像烧录可能重置已有 Wi-Fi/API 设置。** 需要保留设置时，请采用
[固件与数据策略](docs/development/engineering/firmware-layout.zh_CN.md)中的兼容
分段烧录流程。默认分区只有 NVS、PHY data 和一个 factory 应用，没有 OTA 槽位。
不要把单独的应用镜像烧录到 `0x0`。

主机测试和编译通过不代表硬件验收通过。请在自己的设备上检查屏幕、按键、配网、
API 权限和断线重连；续航结论需要实测功耗。

## 当前限制

- 真实模式暂不提供**七日流量总量**，需要先实现经过核验的字节计数来源。
  演示模式的流量柱状图为模拟数据。
- VPN 详情仅表示当前连接，不是全部已配置或历史用户。客户端扫描上限为
  300 条，达到限制会标记部分结果。
- 最多保留八个云端站点、十二个设备；客户端显示页保留六条记录。
  SD-WAN 当前处理一个配置。
- 局域网模式没有云端 ISP 历史或 Site Manager SD-WAN 数据。用于 WAN/VPN 的
  控制器兼容接口可能随 Network 版本变化。
- 事件是本机观察记录，不是 UniFi 完整日志；缺失数据会显示不可用，不当作零。
- 不执行 UniFi 配置写入、网关重启或 SD-WAN 重连命令。熄屏不等于深度睡眠。

接口语义、资源边界、兼容性和已记录的验收结果详见
[应用说明](docs/development/unifi-dashboard.zh_CN.md)。

## 目录说明

| 路径 | 用途 |
| --- | --- |
| `main/unifi_*` | 仪表盘、状态模型、LVGL 绘制、直连 API 客户端和配置网页 |
| `components/bsp/` | 共用硬件驱动与板级接口 |
| `assets/fonts/` | 中文字体源文件、生成的 C 资源与字体许可证 |
| `tests/` | 主机测试、模拟 API 响应和原生 LVGL 预览程序 |
| `tools/` | 验证、固件检查、字体与预览工具 |
| `gateway/` | 可选的旧版 Python 网关；当前固件不使用它 |
| `docs/` | 应用、硬件、编译与贡献文档 |
| `skills/` | 仓库 AI 开发工作流 |

使用 AI 辅助开发时先阅读 [AGENTS.zh_CN.md](AGENTS.zh_CN.md)。其他资料：
[上游平台文档](docs/README.zh_CN.md)、[字体与预览生成](assets/README.zh_CN.md)、
[旧版网关](gateway/README.zh_CN.md)。

## 许可证与致谢

基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，保留其 BSP、开发
工具与上游提交历史。项目代码使用 [MIT 许可证](LICENSE)，内置 Noto Sans CJK
字体使用 [SIL Open Font License 1.1](assets/fonts/unifi_OFL.txt)。
