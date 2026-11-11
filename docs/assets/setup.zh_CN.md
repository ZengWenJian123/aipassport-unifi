<p align="right">
  <strong>简体中文</strong> · <a href="setup.md">English</a>
</p>

# UniFi Passport 烧录与配置

本应用使用 `feature/unifi-dashboard` 分支；仓库的 `main` 分支是上游硬件测试
演示和项目入口。

## 1. 准备固件

克隆应用分支，在已激活 ESP-IDF 5.5.3 的终端中编译：

```bash
git clone --branch feature/unifi-dashboard https://github.com/ZengWenJian123/aipassport-unifi.git
cd aipassport-unifi
./tools/validate.sh --firmware
```

使用验证后的合并镜像 **`build/FoloToy-AI-Passport-full.bin`**。
名称相近的 `FoloToy-AI-Passport.bin` 只是应用镜像，不能写入 `0x0`。
源码与编译后的固件是不同文件；网页烧录工具不会编译 GitHub 仓库。
环境准备见[编译指南](../development/engineering/build-and-test.zh_CN.md)。

## 2. 通过网页烧录

打开官方 [AI Passport 网页烧录工具](https://ai-passport.folotoy.cn/tools/web-flasher/)。

1. 在电脑的 Chrome 或 Edge 中打开页面，用支持**数据传输**的 USB 线连接已开机
   的设备，关闭占用该串口的监视器。
2. 选择**安装本地固件**，点击**连接设备**，在浏览器弹窗中选择 AI Passport 串口。
3. 选择已验证的 `FoloToy-AI-Passport-full.bin`，单文件的烧录地址设为 **`0x0`**。
4. 点击**开始写入**，完成前不要断开 USB。完成后按需重启设备，按屏幕提示连接
   设置热点。

工具也支持多个分区镜像。合并镜像的填充区域可能覆盖 NVS，使原 Wi-Fi/API 设置
丢失。需要保留配置时，使用**同一个已验证固件包**内配套的引导程序、分区表和
应用镜像，按该包记录的偏移分段写入，并确认分区布局兼容。普通更新不要点击
**清除设备数据**。详见[烧录与数据策略](../development/engineering/firmware-layout.zh_CN.md)。

未修改的默认布局使用 `0x0`（引导程序）、`0x8000`（分区表）、`0x10000`（应用）。
请核对固件包的 `manifest.json` 和 `flash_args`；自定义分区布局不能直接照搬地址。
以上网页操作按官方工具当前界面整理。

## 3. 获取 UniFi API Key

使用**云端模式**时，打开 [UniFi API Keys](https://unifi.ui.com/settings/api-keys)，
登录能够访问目标站点的账号。

1. 进入 **Settings → API Keys**，选择 **Create New API Key**。
2. 设置便于识别的名称，例如 `AI Passport`；若当前表单提供站点和应用访问选项，
   选择实际需要监控的范围。
3. 创建后立即复制并妥善保存，完整密钥只显示一次。
4. 在 Passport 配置页选择**云端 Site Manager API**，粘贴密钥，保存并测试，
   然后选择发现的站点。

Network 客户端和设备详情还需要相应的 Network/Connector 访问权限。
菜单细节可能随账号和版本变化，可参考 Ubiquiti 的
[API 入门说明](https://developer.ui.com/network/v11.0.97/gettingstarted)和
[官方 API 概览](https://help.ui.com/hc/en-us/articles/30076656117655-Getting-Started-with-the-Official-UniFi-API)。

**局域网模式单独配置：**请在本地控制台的 Network → Integrations 中查看当前
版本的 API/密钥说明，使用兼容的本地密钥、私有 IPv4 HTTPS 控制台地址，并按需
填写信任证书。不要默认云端密钥能用于局域网接口。证书身份必须匹配控制台 IP，
本应用不会跳过 TLS 校验。

真实密钥不能放入 README、Issue、截图、源码或共享日志，只填写到设备配置页。
如果密钥泄露，应在 UniFi 中撤销并创建新密钥；从 Git 删除文件不会撤销凭据。

## 4. 配置设备

手机连接屏幕显示的 `Passport-...` 热点，默认密码 `12345678`，打开
<http://192.168.4.1>，先配置 2.4 GHz Wi-Fi，再保存并测试 API 配置。
等待网络校时和站点发现，选择站点后关闭热点。密钥输入框留空会保留原值；更换
密钥或 API 地址后会重新发现站点，成功后请重新选择目标。

密钥保存在设备 NVS 中，不会编译进固件，网页也不会将其读回。
当前未启用 NVS/Flash 加密，因此设备 Flash 转储同样属于私有数据。
热点十分钟后自动关闭，可从设置页重新开启。按键操作见[应用 README](../../README.zh_CN.md)。
