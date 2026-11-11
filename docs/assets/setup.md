<p align="right">
  <a href="setup.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Install and configure UniFi Passport

Use the `feature/unifi-dashboard` branch for this application. The repository's
`main` branch contains the upstream hardware demo and project index.

## 1. Obtain the firmware

Clone the application branch and build it in an activated ESP-IDF 5.5.3 terminal:

```bash
git clone --branch feature/unifi-dashboard https://github.com/ZengWenJian123/aipassport-unifi.git
cd aipassport-unifi
./tools/validate.sh --firmware
```

Use the verified **`build/FoloToy-AI-Passport-full.bin`** merged image. The similar
`FoloToy-AI-Passport.bin` is an application-only image and must not be written at
`0x0`. Source code and generated firmware are different deliverables; the web
flasher does not compile a GitHub repository. See the
[build guide](../development/engineering/build-and-test.md) for environment setup.

## 2. Flash through the browser

Use the official [AI Passport web flasher](https://ai-passport.folotoy.cn/tools/web-flasher/).

1. Open it in desktop Chrome or Edge. Connect the powered device with a USB
   **data cable** and close any serial monitor holding the port.
2. Choose **Install local firmware**, then **Connect device** and select the
   AI Passport serial device in the browser prompt.
3. Select the verified merged `FoloToy-AI-Passport-full.bin`, with a single
   entry at **`0x0`**.
4. Start writing, keep USB connected until completion, then restart the device
   if necessary and follow the setup hotspot shown on its screen.

The flasher also accepts multiple partition images. A merged image can overwrite
saved Wi-Fi/API settings because it includes padding over NVS. To preserve an
existing configuration, use matching bootloader, partition-table and application
images from the **same verified bundle**, at its recorded offsets, with a compatible
partition layout. Do not select **Clear device data** for a normal update. See the
[Flash/data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

For the unchanged default layout, segmented images use `0x0` (bootloader),
`0x8000` (partition table) and `0x10000` (application). Verify the bundle's
`manifest.json` and `flash_args`; do not assume these offsets for custom layouts.
The browser installation steps are based on the official flasher's current UI.

## 3. Create a UniFi API key

For **cloud mode**, open [UniFi API Keys](https://unifi.ui.com/settings/api-keys)
and sign in to the account that can access your sites.

1. Go to **Settings → API Keys** and choose **Create New API Key**.
2. Give it a recognizable name, such as `AI Passport`, and select the intended
   sites/application access if the current form offers those options.
3. Copy the new key immediately and save it privately; it is shown only once.
4. In the Passport setup page, choose **Cloud Site Manager API**, paste the key,
   save/test, and select a discovered site.

Network client/device details also need suitable Network/Connector access.
Menu details can vary by account or version. See Ubiquiti's
[API onboarding](https://developer.ui.com/network/v11.0.97/gettingstarted) and
[official API overview](https://help.ui.com/hc/en-us/articles/30076656117655-Getting-Started-with-the-Official-UniFi-API).

**LAN mode is separate:** consult the local console's Network → Integrations
page for its version-specific API/key instructions, use a compatible local key
and the private IPv4 HTTPS console URL, and provide its trust certificate when
needed. Do not assume a cloud key works locally. The certificate must match the
console IP; this application does not disable TLS verification.

Never put a real key in a README, issue, screenshot, source file or shared log.
Enter it only in the device setup page. If a key is exposed, revoke it in UniFi
and create a replacement; removing a file from Git does not revoke a credential.

## 4. Configure the device

Connect the phone to the `Passport-...` hotspot shown on screen; its default
password is `12345678`. Open <http://192.168.4.1>, configure 2.4 GHz Wi-Fi, then
save/test the API configuration. Wait for network time and site discovery,
select a site and close the hotspot. A blank secret field keeps the saved value.
Changing credentials or the API endpoint triggers site rediscovery; choose the
new target after it succeeds.

The key is stored in device NVS rather than compiled into the firmware. The
setup page does not read it back. NVS/Flash encryption is not enabled, so a device
Flash dump is private too. The hotspot closes after ten minutes and can be
reopened from Settings. See the [application README](../../README.md) for controls.
