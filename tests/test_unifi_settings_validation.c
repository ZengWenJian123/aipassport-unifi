#include "unifi_settings_validation.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const char *flat = "{\"ssid\":\"a { \\\" [] name\",\"demo\":false}";
    assert(unifi_config_json_shape_valid(flat, strlen(flat)));
    assert(!unifi_config_json_shape_valid("{\"a\":{\"b\":0}}", 13));
    assert(!unifi_config_json_shape_valid("{\"a\":[]}", 8));
    assert(!unifi_config_json_shape_valid("{\"a\":\"bad", 9));
    const char *good[] = {"https://10.0.0.1", "https://192.168.10.1:443", "https://172.16.0.1/", "https://172.31.255.254:8443/"};
    const char *bad[] = {"http://192.168.1.1", "https://api.ui.com", "https://8.8.8.8", "https://127.0.0.1", "https://169.254.1.1", "https://172.32.0.1", "https://192.169.1.1", "https://192.168.1.1/api", "https://192.168.1.1?x=1", "https://192.168.1.1#x", "https://user@192.168.1.1", "https://192.168.1.1:0", "https://192.168.1.1:65536", "https://010.0.0.1", "https://10.0.0.999", "https://10.0.0.1\r\nx", "https://4294967306.0.0.1", "https://10.0.0.1//"};
    for (size_t i = 0; i < sizeof(good)/sizeof(*good); ++i) assert(unifi_lan_url_valid(good[i]));
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) assert(!unifi_lan_url_valid(bad[i]));
    assert(!unifi_lan_url_valid("https://10.0.0.1:4294967739"));
    unifi_config_t config = {0}; config.poll_seconds = 30; config.brightness = 70;
    assert(unifi_config_valid(&config));
    const uint32_t timeouts[]={0,30,60,120,300};
    for(unsigned i=0;i<sizeof timeouts/sizeof timeouts[0];i++) {
        config.screen_timeout_sec=timeouts[i];assert(unifi_config_valid(&config));
    }
    config.screen_timeout_sec=15;assert(!unifi_config_valid(&config));
    config.screen_timeout_sec=UINT32_MAX;assert(!unifi_config_valid(&config));
    config.screen_timeout_sec=UNIFI_SCREEN_TIMEOUT_DEFAULT;
    /* A pre-upgrade config_v1 blob must not consume the new setting or discard
     * credentials. Its old byte count is the on-device compatibility contract. */
    assert(UNIFI_CONFIG_V1_BYTES==2700);
    unifi_config_t legacy=config, restored={.screen_timeout_sec=60};
    strcpy(legacy.ssid,"saved-network");strcpy(legacy.api_key,"fake-saved-key");
    memcpy(&restored,&legacy,UNIFI_CONFIG_V1_BYTES);
    assert(restored.screen_timeout_sec==60 && unifi_config_valid(&restored));
    assert(!strcmp(restored.ssid,"saved-network")&&!strcmp(restored.api_key,"fake-saved-key"));
    strcpy(config.api_key, "demo-key_not-real"); assert(unifi_config_valid(&config));
    strcpy(config.api_key, "key\r\nInjected: header"); assert(!unifi_config_valid(&config)); config.api_key[0] = 0;
    strcpy(config.password, "short"); assert(!unifi_config_valid(&config));
    memset(config.password, 'a', 64); config.password[64] = 0; assert(unifi_config_valid(&config));
    config.password[3] = 'z'; assert(!unifi_config_valid(&config)); config.password[0] = 0;
    memset(config.ssid, 'a', sizeof(config.ssid)); assert(!unifi_config_valid(&config)); config.ssid[32] = 0; assert(unifi_config_valid(&config));
    config.local_api = true; assert(!unifi_config_valid(&config));
    strcpy(config.console_url, "https://192.168.10.1"); assert(unifi_config_valid(&config));
    strcpy(config.ca_pem, "not a cert"); assert(!unifi_config_valid(&config)); config.ca_pem[0] = 0;
    config.poll_seconds = 10; assert(unifi_config_valid(&config)); config.poll_seconds = 9; assert(!unifi_config_valid(&config)); config.poll_seconds = 3601; assert(!unifi_config_valid(&config)); config.poll_seconds = 30;
    config.brightness = 0; assert(!unifi_config_valid(&config)); config.brightness = 101; assert(!unifi_config_valid(&config)); config.brightness = 70;
    strcpy(config.host_id, "abcd:1234"); strcpy(config.site_id, "some-site_id"); assert(unifi_config_valid(&config));
    strcpy(config.site_id, "../other/path"); assert(!unifi_config_valid(&config));
    /* Credential rotation/account transfer must not retain the previous target.
     * Blank-key saves are already merged to the previous key before this call. */
    unifi_config_t old={.poll_seconds=30,.brightness=70,.screen_timeout_sec=60};
    strcpy(old.api_key,"fake-account-a");strcpy(old.host_id,"account-a-host");strcpy(old.site_id,"account-a-site");
    unifi_config_t next=old;
    next.brightness=80;unifi_config_reset_target_on_source_change(&old,&next);
    assert(!strcmp(next.site_id,old.site_id));
    strcpy(next.api_key,"fake-account-b");unifi_config_reset_target_on_source_change(&old,&next);
    assert(!next.host_id[0]&&!next.site_id[0]&&!strcmp(next.api_key,"fake-account-b"));
    next=old;next.api_key[0]=0;unifi_config_reset_target_on_source_change(&old,&next);
    assert(!next.host_id[0]&&!next.site_id[0]);
    next=old;next.local_api=true;unifi_config_reset_target_on_source_change(&old,&next);assert(!next.site_id[0]);
    old.local_api=true;strcpy(old.console_url,"https://10.0.0.1");next=old;
    strcpy(next.console_url,"https://10.0.0.2");unifi_config_reset_target_on_source_change(&old,&next);assert(!next.site_id[0]);
    next=old;strcpy(next.site_id,"another-site");unifi_config_reset_target_on_source_change(&old,&next);
    assert(!strcmp(next.site_id,"another-site"));
    puts("UniFi settings validation: PASS (LAN allowlist, lengths, IDs, header injection, ranges)");
    return 0;
}
