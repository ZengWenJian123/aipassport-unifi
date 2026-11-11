<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### UniFi 仪表盘中文字库

[`fonts/unifi_fonts.h`](fonts/unifi_fonts.h) 声明应用专用字体
`unifi_font_12`、`unifi_font_14`、`unifi_font_18`、`unifi_font_24`。
将对应的 `.c` 文件编译到应用中，并在每个文字控件上明确选择字体。
这些资源不会替换 BSP 字体。

| 字体 | 行高 / 从行底计算的基线 | 覆盖范围 | 位图字节数 |
| --- | --- | --- | --- |
| `unifi_font_12` | 16 / 4 px | 6,893 个码点 | 515,553 |
| `unifi_font_14` | 18 / 5 px | 6,893 个码点 | 679,001 |
| `unifi_font_18` | 22 / 5 px | 6,893 个码点 | 1,104,448 |
| `unifi_font_24` | 28 / 7 px | 95 个可打印 ASCII 字符 | 10,627 |

三个中文字号均覆盖 GB2312 一级、二级的全部 6,763 个汉字、ASCII、标点、
箭头和应用固定文案，不代表支持任意 Unicode 姓名或 emoji。
动态内容的未支持码点须替换为 `?`，不能截断 UTF-8 字符；
`unifi_font_has_codepoint()` 用于检查实际字体覆盖。
24 px 只用于 ASCII 数字和单位，中文单位应另用较小字号；图标单独绘制。

可再分发的固定输入为
[`fonts/unifi_NotoSansCJKsc-Regular.otf`](fonts/unifi_NotoSansCJKsc-Regular.otf)，
即 Noto Sans CJK SC Regular 2.004，来自
[官方上游提交](https://github.com/notofonts/noto-cjk/tree/523d033d6cb47f4a80c58a35753646f5c3608a78)。
SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。
源字体与派生字体使用 [SIL OFL 1.1](fonts/unifi_OFL.txt)，再分发时保留该许可。
16 MB 的源 OTF 只是生成素材，**不嵌入固件**。

[`tools/generate_unifi_fonts.py`](../tools/generate_unifi_fonts.py) 使用
Pillow 11.3.0 / FreeType 2.13.3 栅格化，再写入原始 4bpp：高半字节在前、
字形起点字节对齐、行末不填充。cmap 采用连续 ASCII 的 `FORMAT0_TINY`
加排序后的 `SPARSE_TINY`。位图、字形描述符与 cmap 数组均为 Flash 中的
`const`。须启用 `CONFIG_LV_FONT_FMT_TXT_LARGE=y`，因为 18 px 位图超过
默认 20 位索引上限。四种字体连同大字形描述符约占 2.68 MB Flash，运行时仅需
单字形绘制缓冲；最终链接大小及堆占用仍须在目标设备核对。

```bash
python3 -m venv build/font-tools
build/font-tools/bin/pip install Pillow==11.3.0 fonttools==4.60.1
build/font-tools/bin/python tools/generate_unifi_fonts.py
python3 tools/generate_unifi_fonts.py --check
python3 tests/test_unifi_fonts.py
build/font-tools/bin/python tools/generate_unifi_fonts.py --check --verify-raster
```

生成器读取 `main/unifi*.c`、`main/unifi*.h` 的字符串字面量（包含
`unifi_view.c`），并保留
[`fonts/unifi_font_manifest.json`](fonts/unifi_font_manifest.json)
中已有的额外码点。新固定文案必须通过 12、14、18 px 的覆盖检查。
仅用 Python 标准库即可验证真实 C 文件中的 cmap、字形边界、基线、字节哈希、
非空字形、缺字反例和奇数宽度的半字节打包。栅格验证会回读每个 C 字形，
解码后逐像素对照重新栅格化的源字体。生成器还在已忽略的
`build/unifi-fonts/` 下输出从 C 位图绘制的样张 PNG 和 JSON 报告。
这些主机检查不等于真机字形显示验证。

还可经过**实际 LVGL 9.5 渲染器**验证：安装项目托管 LVGL 组件后运行
`build/font-tools/bin/python tools/render_unifi_preview.py`。
该工具编译与固件相同的应用视图、绘制代码和字体，使用 24 KiB LVGL 堆及
RGB565 分块帧缓冲，测试 UTF-8 省略，并将代表字形的实渲染像素与原始
4bpp 位图逐像素对照。九个主页面和详情、异常、配网状态的原生 240 × 320
PNG 保存在已忽略的 `build/unifi-preview/`，另有最近邻放大的拼图供人工
检查。需 CMake、主机 C/C++ 编译器及 Pillow，不访问网络或连接设备。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
