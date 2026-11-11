/* Optional browser host test. Install Playwright in a temporary npm prefix,
 * then set NODE_PATH=<prefix>/node_modules. CHROME_BIN may select an existing
 * Chromium browser; otherwise Playwright uses its installed Chromium.
 * All requests are intercepted. No device, API or secret is used.
 * Example: NODE_PATH=/tmp/passport-browser/node_modules node tests/test_unifi_portal.cjs
 */
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

(async () => {
  const options = { headless: true };
  if (process.env.CHROME_BIN) options.executablePath = process.env.CHROME_BIN;
  const browser = await chromium.launch(options);
  try {
    const dir = process.env.UNIFI_PORTAL_ARTIFACT_DIR || fs.mkdtempSync(path.join(os.tmpdir(), 'passport-portal-'));
    fs.mkdirSync(dir, { recursive: true });
    const page = await browser.newPage({ viewport: { width: 360, height: 1000 } });
    page.setDefaultTimeout(10000);
    const errors = [], unexpected = [];
    page.on('pageerror', error => errors.push(error.message));
    const html = fs.readFileSync(path.resolve(__dirname, '../main/unifi_portal.html'), 'utf8');
    const status = {
      wifi_connected: false, time_valid: false, ssid: '', ip: '', error: '',
      password_set: false, api_key_set: false, ca_set: false, local_api: false,
      demo: false, console_url: '', host_id: '', site_id: '', poll_seconds: 30,
      brightness: 70, generation: 1, save_sequence: 0, saving: false,
      save_error: '', api_ok: false, api_refreshing: false, api_http: 0, api_epoch: 0, api_error: '',
      connection_tested: false, connection_ok: false, connection_http: 0, connection_epoch: 0, connection_error: '',
      scanning: false, scan_error: '',
      networks: [{ ssid: 'Example IOT', rssi: -50, open: false }], sites: [],
    };
    let rejectSave = false, posts = 0, failedStatus = 0, closed = false, failStatusOnce = true;
    const apiRequests = [];
    await page.route('**/*', async route => {
      const req = route.request(), url = new URL(req.url());
      if (url.origin !== 'http://192.168.4.1') {
        unexpected.push(url.origin); return route.abort();
      }
      if (url.pathname === '/') return route.fulfill({ contentType: 'text/html', body: html });
      apiRequests.push({ path: url.pathname, headers: req.headers() });
      if (url.pathname === '/api/status' && failStatusOnce) {
        failStatusOnce = false; failedStatus++;
        return route.fulfill({ status: 503, contentType: 'application/json', body: '{"error":"temporary failure"}' });
      }
      if (url.pathname === '/api/config') {
        posts++; const data = req.postDataJSON(); status.save_sequence++;
        if (rejectSave) status.save_error = 'NVS save failed';
        else {
          status.save_error = ''; status.generation++;
          Object.assign(status, Object.fromEntries(Object.entries(data).filter(([k]) => !['password', 'api_key', 'ca_pem'].includes(k))));
          if (data.password) status.password_set = true;
          if (data.api_key) {
            status.api_key_set = true; status.api_ok = true; status.host_id = ''; status.site_id = '';
            Object.assign(status, {connection_tested:true,connection_ok:true,connection_http:200,connection_epoch:1794110400});
            status.sites = [{ name: 'Example Office', host_id: 'host-1', site_id: 'site-1' }];
          }
          if (data.ssid) { status.wifi_connected = true; status.time_valid = true; status.ip = '192.168.10.88'; }
        }
        return route.fulfill({ status: 202, contentType: 'application/json', body: '{"queued":true}' });
      }
      if (url.pathname === '/api/close') closed = true;
      else assert.ok(['/api/status', '/api/scan'].includes(url.pathname), 'Unexpected endpoint');
      return route.fulfill({ contentType: 'application/json', body: JSON.stringify(status) });
    });
    await page.goto('http://192.168.4.1/');
    await page.locator('#settings').waitFor({ state: 'visible' });
    assert.equal(await page.locator('#token, #unlock, #unlock-button').count(), 0, 'No setup-code interaction');
    await page.waitForFunction(() => document.querySelector('#portal-state').textContent.includes('暂时不可达'));
    assert.ok(await page.locator('#ssid').isDisabled(), 'Do not overwrite saved settings before the initial read');
    await page.locator('#refresh-status').click();
    await page.waitForFunction(() => !document.querySelector('#settings').disabled);
    assert.equal(await page.locator('#api-state').textContent(), '等待 Wi-Fi 连接');
    assert.ok(await page.locator('#sites').isDisabled());
    async function checkState(patch, expected, hint) {
      Object.assign(status, patch);
      await page.locator('#refresh-status').click();
      await page.waitForFunction(text => document.querySelector('#api-state').textContent === text, expected);
      assert.ok((await page.locator('#site-hint').textContent()).includes(hint));
      assert.equal(await page.locator('#sites option').first().textContent(), expected, 'Empty list explains why, rather than claiming no sites');
    }
    await checkState({ wifi_connected: true }, '尚未配置 API Key', '输入 API Key');
    await checkState({ api_key_set: true, time_valid: false }, '等待网络校时', '校准时间');
    await checkState({ time_valid: true }, '正在查询站点', '页面会自动更新');
    await checkState({ api_refreshing: true }, '正在查询站点', '页面会自动更新');
    await checkState({ api_refreshing: false, api_error: 'No sites available for this API key' }, '未发现可访问站点', '所属账号');
    for (const [code, expected, hint] of [
      [401, 'API 身份验证失败', 'API Key'], [403, 'API 访问被拒绝', '权限'],
      [429, 'API 请求受限，等待重试', '稍后重试'],
    ]) {
      await checkState({ api_http: code, api_error: `Sites: HTTP ${code}` }, expected, hint);
      assert.equal(await page.locator('#api-detail').textContent(), `诊断：Sites: HTTP ${code}`);
    }
    await checkState({ api_http: 0, api_error: 'Sites: ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME' }, '域名解析失败', 'DNS');
    await checkState({ api_error: 'Sites: ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED' }, 'HTTPS 连接失败', '证书');
    await checkState({ api_error: 'Sites: ESP_ERR_TIMEOUT' }, 'API 连接超时', '等待重试');
    await checkState({ api_error: 'Sites: ESP_FAIL' }, 'API 查询未完成', '尚不能判断');
    await checkState({ api_ok: true, api_http: 200, api_error: '' }, '查询成功，未返回站点', '未返回可用站点');
    assert.equal(await page.locator('#api-detail').textContent(), '暂无错误');
    Object.assign(status, { wifi_connected: false, time_valid: false, api_key_set: false, api_ok: false, api_http: 0 });
    await page.locator('#ssid').fill('Example IOT');
    await page.locator('#password').fill('fake-test-password');
    await page.locator('#save-wifi').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent.includes('Wi-Fi 配置已保存'));
    assert.equal(await page.locator('#password').inputValue(), '', 'Password must clear');
    await page.screenshot({ path: path.join(dir, 'portal-wifi.png'), fullPage: true });
    await page.locator('#to-api').click();
    await page.locator('#api-key').fill('fake-test-key');
    await page.locator('#save-api').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent.includes('API 配置已保存'));
    assert.equal(await page.locator('#api-key').inputValue(), '', 'Key must clear');
    assert.equal(await page.locator('#api-state').textContent(), '本轮更新完成');
    assert.ok(await page.locator('#sites').isEnabled());
    await page.locator('#save-api').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent.includes('API 配置已保存'));
    assert.ok(status.api_key_set, 'Blank key must preserve the saved key');
    await page.locator('#sites').selectOption('0');
    await page.locator('#save-site').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent === '已保存到设备。');
    assert.equal(status.site_id, 'site-1');
    // Backend resets an old account's target; the filled form must follow it.
    await page.locator('#api-key').fill('fake-different-account');
    await page.locator('#save-api').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent.includes('API 配置已保存'));
    assert.equal(await page.locator('#host-id').inputValue(), '');
    assert.equal(await page.locator('#site-id').inputValue(), '');
    assert.equal(await page.locator('#api-key').inputValue(), '');
    Object.assign(status, { api_refreshing: true, api_error: '',query_stage:'Clients',query_count:4,query_http:200,query_duration_ms:1250 });
    await page.locator('#refresh-status').click();
    await page.waitForFunction(() => document.querySelector('#api-state').textContent === '正在更新监控数据');
    assert.match(await page.locator('#connection-result').textContent(), /连接测试成功/);
    assert.match(await page.locator('#connection-time').textContent(), /最近测试/);
    assert.match(await page.locator('#query-detail').textContent(), /客户端 \/ VPN.*1.3 秒.*第 4 项/);
    assert.equal(await page.locator('#sites').inputValue(), '0', 'Saved site remains selected');
    assert.ok(await page.locator('#sites').isEnabled(), 'Sites can be selected before the remaining API requests finish');
    Object.assign(status, { api_refreshing: false, api_ok: false, api_error: 'Selected site unavailable; choose a discovered site' });
    await page.locator('#refresh-status').click();
    await page.waitForFunction(() => document.querySelector('#api-state').textContent === '原监控站点不可用');
    assert.ok(await page.locator('#sites').isEnabled(), 'A stale selected site can be replaced');
    Object.assign(status, { api_ok: false, api_http: 403, api_error: 'Clients: HTTP 403' });
    await page.locator('#refresh-status').click();
    await page.waitForFunction(() => document.querySelector('#api-state').textContent === '部分数据暂不可用');
    assert.equal(await page.locator('#sites option').count(), 2, 'A later API failure must retain discovered sites');
    assert.ok(await page.locator('#sites').isEnabled());
    assert.match(await page.locator('#connection-result').textContent(), /连接测试成功/, 'Client permission failure is distinct from the site connection test');
    Object.assign(status, {connection_ok:false,connection_http:401,connection_error:'Connection test: HTTP 401'});
    await page.locator('#refresh-status').click();
    await page.waitForFunction(() => document.querySelector('#connection-result').textContent.includes('连接测试失败'));
    assert.match(await page.locator('#connection-result').textContent(), /401/);
    Object.assign(status, { connection_ok:true,connection_http:200,connection_error:'',api_ok: true, api_http: 200, api_error: '' });
    await page.locator('#refresh-status').click();
    await page.waitForFunction(() => document.querySelector('#connection-result').textContent.includes('连接测试成功'));
    await page.screenshot({ path: path.join(dir, 'portal-api.png'), fullPage: true });
    await page.locator('#api-key').fill('fake-unsaved-key');
    await page.locator('#to-display').click();
    assert.equal(await page.locator('#api-key').inputValue(), '', 'Tab changes must clear unsaved secrets');
    assert.equal(await page.locator('#screen-timeout').inputValue(),'60','Older saved settings default to one minute');
    await page.locator('#screen-timeout').selectOption('30');
    await page.locator('#poll').selectOption('10');
    rejectSave = true;
    await page.locator('#save-display').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent.includes('保存失败'));
    assert.equal(status.poll_seconds, 30, 'Failed save must not apply settings');
    assert.equal(status.screen_timeout_sec,undefined,'Failed save must not apply the new screen timeout');
    rejectSave = false;
    await page.locator('#save-display').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent === '已保存到设备。');
    assert.equal(status.poll_seconds,10);
    assert.equal(status.screen_timeout_sec,30);
    await page.reload();
    await page.waitForFunction(() => !document.querySelector('#settings').disabled);
    await page.locator('[data-step="2"]').click();
    assert.equal(await page.locator('#screen-timeout').inputValue(),'30','Saved timeout survives a page reload');
    await page.locator('#screen-timeout').selectOption('0');
    await page.locator('#save-display').click();
    await page.waitForFunction(() => document.querySelector('#message').textContent === '已保存到设备。');
    assert.equal(status.screen_timeout_sec,0,'Always-on zero must not fall back to sixty seconds');
    await page.locator('#refresh-status').click();
    assert.equal(await page.locator('#screen-timeout').inputValue(),'0');
    const dimensions = [];
    for (const width of [320, 360, 736]) {
      await page.setViewportSize({ width, height: 1000 });
      for (const step of [0, 1, 2]) {
        await page.locator(`[data-step="${step}"]`).click();
        const size = await page.evaluate(() => ({ width: innerWidth, scroll: document.documentElement.scrollWidth }));
        assert.equal(size.width, size.scroll, 'Horizontal overflow'); dimensions.push({ step, ...size });
      }
    }
    await page.setViewportSize({ width: 360, height: 1000 });
    await page.screenshot({ path: path.join(dir, 'portal-display.png'), fullPage: true });
    assert.equal(await page.evaluate(() => localStorage.length + sessionStorage.length), 0, 'No persisted browser settings');
    await page.locator('#close').click(); assert.ok(closed, 'Hotspot close requested');
    assert.deepEqual(errors, []); assert.deepEqual(unexpected, []);
    assert.ok(apiRequests.length > 0);
    assert.ok(apiRequests.every(req => !('x-setup-token' in req.headers)), 'No setup-code header on any API request');
    await page.screenshot({ path: path.join(dir, 'portal-complete.png') });
    const report = { pass: true, errors, posts, failedStatus, closed, dimensions,
      tests: 'no-code startup, status failure/retry, Wi-Fi/clock/API/empty-site states, cached sites, Wi-Fi save, blank key preservation, secret clearing, site selection, NVS failure/retry, no browser persistence, hotspot close' };
    fs.writeFileSync(path.join(dir, 'portal-report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify({ ...report, artifacts: dir }));
  } finally { await browser.close(); }
})().catch(error => { console.error(error.stack || error.message); process.exitCode = 1; });
