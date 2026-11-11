#include "unifi_api.h"
#include "esp_http_client.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void unifi_api_reset_cache(void);
static int mode, calls, clouds, metric_calls, connections, cleanups, stale_once, stale_headers, cold_once, mappings, sd_configs, wan_calls;
static int64_t clock_seconds=1000;
static bool fail_wan_links, reorder_sites;
static int sd_status_calls;
struct mock_client { esp_http_client_config_t cfg; char body[2048];size_t pos;int status; };
int64_t esp_timer_get_time(void){return clock_seconds*1000000;}
const char *esp_err_to_name(esp_err_t code){(void)code;return "mock error";}
esp_err_t esp_crt_bundle_attach(void *x){(void)x;return 0;}
static void response_for(struct mock_client *c,const esp_http_client_config_t *cfg){
    c->cfg=*cfg;c->pos=0;c->body[0]=0;c->status=200;calls++;
    bool cloud=strstr(cfg->url,"api.ui.com")!=NULL;if(cloud)clouds++;
    if(mode==8&&(strstr(cfg->url,"/stat/health")||strstr(cfg->url,"/stat/remoteuservpn"))){c->status=503;return;}
    if(mode==1){c->status=429;return;}
    if(fail_wan_links&&strstr(cfg->url,"/stat/device/")){c->status=503;return;}
    if(mode==2&&strstr(cfg->url,"/clients")){c->status=403;return;}
    if(mode==6&&strstr(cfg->url,"/v1/sites")){c->status=401;return;}
    if(mode==5&&strstr(cfg->url,"https://api.ui.com/v1/sites")){c->status=503;return;}
    if(mode>=10&&strstr(cfg->url,"sd-wan-configs")){
        if(strstr(cfg->url,"/status")){
            sd_status_calls++;
            if(mode==12&&strstr(cfg->url,"mesh-other")){c->status=403;return;}
            bool belongs=strstr(cfg->url,"mesh-good")||mode==11;
            snprintf(c->body,sizeof(c->body),"{\"data\":{\"hubs\":[{\"hostId\":\"%s\",\"siteId\":\"%s\"}],\"spokes\":[{\"connections\":[{\"tunnels\":[{\"status\":\"connected\"}]}]}]}}",belongs||mode==13?"host-a":"other-host",belongs&&mode!=13?"site-a":"other-site");
        }else{
            sd_configs++;
            if(mode==10)strcpy(c->body,"{\"data\":[{\"id\":\"mesh-other\",\"type\":\"sdwan-hbsp\"},{\"id\":\"mesh-other2\",\"type\":\"sdwan-hbsp\"},{\"id\":\"mesh-good\",\"type\":\"sdwan-hbsp\"}]}");
            else strcpy(c->body,"{\"data\":[{\"id\":\"mesh-other\",\"type\":\"sdwan-hbsp\"},{\"id\":\"mesh-good\",\"type\":\"sdwan-hbsp\"}]}");
        }
        return;
    }
    if(reorder_sites&&strstr(cfg->url,"https://api.ui.com/v1/sites")){
        strcpy(c->body,"{\"data\":[{\"hostId\":\"host-b\",\"siteId\":\"site-b\",\"meta\":{\"name\":\"branch\",\"desc\":\"Branch\"}},{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"meta\":{\"name\":\"default\",\"desc\":\"Default\"}}]}");return;
    }
    if(mode>=7&&strstr(cfg->url,"sd-wan-configs")&&strstr(cfg->url,"/status"))strcpy(c->body,"{\"data\":{\"status\":{\"connections\":[{\"sites\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"status\":{\"connected\":true,\"offline\":false}},{\"hostId\":\"host-b\",\"siteId\":\"site-b\",\"status\":{\"connected\":true,\"offline\":false}}]}]}}}");
    else if(mode>=7&&strstr(cfg->url,"sd-wan-configs")){sd_configs++;strcpy(c->body,"{\"data\":[{\"id\":\"mesh-a\",\"type\":\"s2s-vpn\"}]}");}
    else if(mode>=7&&strstr(cfg->url,"/clients"))strcpy(c->body,"{\"totalCount\":1,\"data\":[{\"id\":\"c-b\",\"name\":\"Desk\",\"type\":\"WIRED\"}]}");
    else if(strstr(cfg->url,"/stat/remoteuservpn"))strcpy(c->body,"{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"id\":\"remote-one\",\"user\":\"Laptop\",\"tunnel_ip\":\"192.0.2.10\",\"up\":true,\"vpn_type\":\"wireguard-server\",\"uptime\":20}]}");
    else if(strstr(cfg->url,"/v1/hosts")){strcpy(c->body,"{\"data\":[{\"id\":\"host-a\",\"reportedState\":{\"name\":\"Example Office\"}}]}");}
    else if(strstr(cfg->url,"/stat/health")){wan_calls++;strcpy(c->body,"{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"subsystem\":\"wan\",\"gw_mac\":\"aa:bb:cc:dd:ee:ff\",\"rx_bytes-r\":100000,\"tx_bytes-r\":20000}]}");}
    else if(strstr(cfg->url,"/stat/device/")){assert(strstr(cfg->url,"/stat/device/aa:bb:cc:dd:ee:ff"));strcpy(c->body,"{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"type\":\"udm\",\"mac\":\"aa:bb:cc:dd:ee:ff\",\"wan1\":{\"up\":true,\"enable\":true},\"wan2\":{\"up\":true,\"enable\":true},\"last_wan_status\":{\"WAN\":\"online\",\"WAN2\":\"online\"}}]}");}
    else if(strstr(cfg->url,"/isp-metrics")){
        metric_calls++;
        time_t t=time(NULL)/300*300;char stamp[40];strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",gmtime(&t));
        snprintf(c->body,sizeof(c->body),"{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"periods\":[{\"metricTime\":\"%s\",\"data\":{\"wan\":{\"avgLatency\":4,\"maxLatency\":6,\"packetLoss\":0,\"download_kbps\":100,\"upload_kbps\":20}}}]}]}",stamp);
    }else if(strstr(cfg->url,"sd-wan-configs"))strcpy(c->body,"{\"data\":[]}");
    else if(strstr(cfg->url,"statistics/latest"))strcpy(c->body,"{\"uptimeSec\":120,\"cpuUtilizationPct\":10,\"memoryUtilizationPct\":30,\"uplink\":{\"rxRateBps\":100,\"txRateBps\":40},\"interfaces\":{}}");
    else if(strstr(cfg->url,"/clients"))strcpy(c->body,"{\"totalCount\":2,\"data\":[{\"id\":\"c-a\",\"name\":\"VPN\",\"type\":\"VPN\",\"ipAddress\":\"192.168.13.1\",\"connectedAt\":\"2026-01-01T00:00:00Z\"},{\"id\":\"c-b\",\"name\":\"Desk\",\"type\":\"WIRED\"}]}");
    else if(strstr(cfg->url,"integration/v1/sites/uuid-a/devices"))strcpy(c->body,"{\"totalCount\":1,\"data\":[{\"id\":\"dev-a\",\"name\":\"Router\",\"state\":\"ONLINE\"}]}");
    else if(strstr(cfg->url,"integration/v1/sites")){mappings++;strcpy(c->body,"{\"totalCount\":1,\"data\":[{\"id\":\"uuid-a\",\"internalReference\":\"default\",\"name\":\"Local test\"}]}");}
    else if(strstr(cfg->url,"/v1/devices"))strcpy(c->body,"{\"data\":[{\"hostId\":\"host-a\",\"devices\":[{\"id\":\"mac-a\",\"name\":\"Router\",\"status\":\"online\",\"startupTime\":\"2026-01-01T00:00:00Z\"}]}]}");
    else if(mode==3)strcpy(c->body,"{\"data\":[]}");
    else if(mode==4)strcpy(c->body,"{\"data\":[{\"hostId\":\"host-b\",\"siteId\":\"site-b\",\"meta\":{\"name\":\"default\",\"desc\":\"Other site\"}}]}");
    else strcpy(c->body,"{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"meta\":{\"name\":\"default\",\"desc\":\"Default\"},\"statistics\":{\"counts\":{\"wifiClient\":0,\"wiredClient\":1,\"totalDevice\":1,\"offlineDevice\":0}}}]}");
    return;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    assert(cfg->method==HTTP_METHOD_GET&&cfg->disable_auto_redirect);
    assert(cfg->crt_bundle_attach||cfg->cert_pem);assert(!strncmp(cfg->url,"https://",8));
    struct mock_client *c=calloc(1,sizeof(*c));assert(c);connections++;response_for(c,cfg);return c;
}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c,const char *url){
    esp_http_client_config_t cfg=c->cfg;cfg.url=url;response_for(c,&cfg);return 0;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *key,const char *value){(void)c;assert(strcmp(key,"X-API-KEY")||!strcmp(value,"fake-test-key"));return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int length){assert(length==0);if(cold_once){cold_once=0;return ESP_FAIL;}if(stale_once&&strstr(c->cfg.url,"/clients")){stale_once=0;return ESP_FAIL;}return 0;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c){
    if(stale_headers&&strstr(c->cfg.url,"/clients")){stale_headers=0;c->status=-1;return -1;}
    if(c->status==429){esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.header_key="Retry-After",.header_value="9",.user_data=c->cfg.user_data};c->cfg.event_handler(&e);}
    return (int64_t)strlen(c->body);
}
int esp_http_client_get_status_code(esp_http_client_handle_t c){return c->status;}
int esp_http_client_read(esp_http_client_handle_t c,char *data,int len){size_t left=strlen(c->body)-c->pos;size_t take=left<17?left:17;if(take>(size_t)len)take=(size_t)len;memcpy(data,c->body+c->pos,take);c->pos+=take;return (int)take;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){return c->pos==strlen(c->body);}
esp_err_t esp_http_client_close(esp_http_client_handle_t c){(void)c;return 0;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c){cleanups++;free(c);return 0;}
typedef struct { int count, calls_at; unsigned site_count, selected_site; bool api_ok, metrics_valid, clients_early; char site_id[64]; } progress_capture_t;
static void capture_progress(const unifi_snapshot_t *s,void *context){
    progress_capture_t *capture=context;
    assert(s->refreshing);
    if(s->clients.available&&!metric_calls)capture->clients_early=true;
    if(capture->count++)return;
    capture->calls_at=calls;capture->site_count=s->site_count;
    capture->selected_site=s->selected_site;capture->api_ok=s->api_ok;capture->metrics_valid=s->metrics_valid;
    if(s->site_count)snprintf(capture->site_id,sizeof(capture->site_id),"%s",s->sites[0].site_id);
}
int main(void){
    unifi_config_t config={0};unifi_snapshot_t snapshot={0};strcpy(config.api_key,"fake-test-key");strcpy(config.host_id,"host-a");strcpy(config.site_id,"site-a");
    progress_capture_t progress={0};
    unifi_api_reset_cache();assert(unifi_api_poll_with_progress(&config,&snapshot,0,false,0,capture_progress,&progress)==ESP_OK);
    assert(progress.count>=3&&progress.calls_at==1&&calls>progress.calls_at);
    assert(progress.clients_early);
    assert(connections==1&&cleanups==1);
    assert(!strcmp(snapshot.sites[0].name,"Example Office"));
    assert(snapshot.connection_tested&&snapshot.connection_ok&&snapshot.connection_http==200);
    assert(progress.site_count==1&&progress.api_ok&&!progress.metrics_valid&&!strcmp(progress.site_id,"site-a"));
    assert(!snapshot.refreshing);
    assert(snapshot.clients.available&&snapshot.clients.complete&&snapshot.clients.total==2&&snapshot.clients.vpn==1&&snapshot.devices[0].rates_valid&&snapshot.metrics_valid);
    assert(!strcmp(snapshot.sites[0].network_site_id,"uuid-a"));
    assert(snapshot.history_count==UNIFI_HISTORY_POINTS);
    assert(snapshot.history_latency[47]==4&&snapshot.history_loss[47]==0);
    assert(isnan(snapshot.history_latency[0])&&isnan(snapshot.history_loss[0]));
    assert(snapshot.wans_available&&snapshot.wans[0].present&&snapshot.wans[1].present&&snapshot.wans_epoch>0);
    int64_t old_wans_epoch=snapshot.wans_epoch;
    fail_wan_links=true;assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(snapshot.wans_epoch==old_wans_epoch&&snapshot.wans[1].present&&strstr(snapshot.wans_error,"503"));
    assert(snapshot.wan_valid); // Failure must not erase valid current rate data.
    fail_wan_links=false;assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);assert(!snapshot.wans_error[0]);
    mode=2;assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(snapshot.clients.total==2&&snapshot.clients.available&&!snapshot.clients.complete&&strstr(snapshot.clients.error,"403"));
    assert(snapshot.history_latency[47]==4&&snapshot.history_loss[47]==0);
    assert(snapshot.connection_ok&&snapshot.connection_http==200&&!snapshot.api_ok);
    assert(!strcmp(snapshot.sites[0].name,"Example Office"));
    mode=1;assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);assert(snapshot.http_status==429&&snapshot.retry_after_sec==9);
    int before=calls;mode=0;assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);assert(calls==before);
    unifi_api_reset_cache();assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);assert(calls==before&&snapshot.http_status==429);
    clock_seconds+=10;assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    /* A valid discovery response must remain selectable after its formerly
     * selected site disappears; never reuse the old site's cached telemetry. */
    mode=4;before=calls;memset(&progress,0,sizeof(progress));
    assert(unifi_api_poll_with_progress(&config,&snapshot,0,false,0,capture_progress,&progress)!=ESP_OK);
    assert(progress.count>=2&&progress.calls_at==before+1&&!progress.api_ok);
    assert(progress.site_count==1&&progress.selected_site==UINT8_MAX&&!strcmp(progress.site_id,"site-b"));
    assert(!progress.metrics_valid&&!snapshot.refreshing);
    assert(calls==before+2&&snapshot.http_status==200&&snapshot.site_count==1);
    assert(!strcmp(snapshot.sites[0].site_id,"site-b")&&snapshot.selected_site==UINT8_MAX);
    assert(strstr(snapshot.error,"Selected site unavailable")&&!snapshot.api_ok);
    assert(!snapshot.metrics_valid&&!snapshot.clients.available&&!snapshot.device_count);
    assert(!snapshot.sdwan.available&&!snapshot.history_count&&snapshot.metric_epoch==0);
    assert(!snapshot.days[0].valid&&snapshot.fetched_epoch>0);
    /* A retry failing before selection resolves must not dereference the
     * UINT8_MAX sentinel or issue a request for a different discovered site. */
    mode=5;before=calls;snapshot.fetched_epoch=123;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(calls==before+1&&snapshot.http_status==503&&strstr(snapshot.error,"Connection test: HTTP 503"));
    assert(snapshot.site_count==1&&snapshot.selected_site==UINT8_MAX);
    assert(snapshot.fetched_epoch==123&&!snapshot.connection_ok&&snapshot.connection_http==503);
    assert(!strcmp(snapshot.sites[0].site_id,"site-b")&&!snapshot.metrics_valid&&!snapshot.clients.available);
    /* The same behavior applies to a stale saved selection on a cold start. */
    mode=4;memset(&snapshot,0,sizeof(snapshot));before=calls;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(calls==before+2&&snapshot.site_count==1&&snapshot.selected_site==UINT8_MAX);
    /* A successful empty list is not an HTTP/parser error or a cached site. */
    mode=3;before=calls;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(calls==before+1&&!snapshot.site_count&&snapshot.http_status==200);
    assert(snapshot.connection_ok&&snapshot.connection_tested);
    assert(snapshot.selected_site==UINT8_MAX&&strstr(snapshot.error,"No sites available"));
    /* Clearing the obsolete selection restores automatic first-site discovery. */
    mode=0;config.host_id[0]=config.site_id[0]=0;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(snapshot.site_count==1&&snapshot.selected_site==0&&snapshot.metrics_valid);
    /* Automatic selection follows stable IDs, not array index/order. */
    reorder_sites=true;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(snapshot.site_count==2&&snapshot.selected_site==1);
    assert(!strcmp(snapshot.sites[1].site_id,"site-a")&&!strcmp(snapshot.sites[1].name,"Example Office"));
    reorder_sites=false;mode=4;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(snapshot.selected_site==UINT8_MAX&&!snapshot.metrics_valid&&!snapshot.clients.available&&!snapshot.wans_available);
    mode=0;
    unifi_api_reset_cache();memset(&snapshot,0,sizeof(snapshot));config.local_api=true;config.host_id[0]=config.site_id[0]=0;strcpy(config.console_url,"https://192.168.10.1");clouds=0;
    before=calls;memset(&progress,0,sizeof(progress));
    assert(unifi_api_poll_with_progress(&config,&snapshot,0,false,0,capture_progress,&progress)==ESP_OK);assert(clouds==0&&snapshot.clients.total==2&&!strcmp(snapshot.sites[0].network_site_id,"uuid-a")&&!strcmp(snapshot.sites[0].name,"Local test"));
    assert(progress.count>=3&&progress.calls_at==before+1&&calls>progress.calls_at);
    assert(progress.site_count==1&&progress.api_ok&&!snapshot.refreshing);
    assert(!snapshot.metrics_valid&&!snapshot.sdwan.available);
    stale_once=1;int previous_connections=connections;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(!stale_once&&connections==previous_connections+2&&connections==cleanups);
    stale_headers=1;previous_connections=connections;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(!stale_headers&&connections==previous_connections+2&&connections==cleanups);
    mode=6;before=calls;snapshot.fetched_epoch=123;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(calls==before+1&&snapshot.connection_tested&&!snapshot.connection_ok&&snapshot.connection_http==401);
    assert(snapshot.fetched_epoch==123);
    config.local_api=false;unifi_api_reset_cache();memset(&snapshot,0,sizeof(snapshot));before=calls;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(calls==before+1&&snapshot.connection_tested&&!snapshot.connection_ok&&snapshot.connection_http==401);
    assert(snapshot.fetched_epoch==0);
    mode=7;config.local_api=false;unifi_api_reset_cache();memset(&snapshot,0,sizeof(snapshot));
    cold_once=1;previous_connections=connections;
    assert(unifi_api_poll(&config,&snapshot,0,true,0)==ESP_OK);
    assert(!cold_once&&connections==previous_connections+2&&connections==cleanups);
    assert(snapshot.wan_valid&&snapshot.wan_down_bps==800000&&snapshot.wan_up_bps==160000&&snapshot.wan_epoch>0&&snapshot.live_count==1);
    assert(snapshot.clients.vpn==1&&snapshot.clients.total==2&&snapshot.clients.vpn_complete&&snapshot.clients.returned==1);
    assert(!strcmp(snapshot.clients.items[0].ip,"192.0.2.10")&&snapshot.clients.latest_vpn_valid);
    assert(snapshot.sdwan.available&&snapshot.sdwan.connected==1&&!strcmp(snapshot.sdwan.type,"Mesh"));
    int previous_mappings=mappings,previous_configs=sd_configs,previous_wan=wan_calls;
    assert(unifi_api_poll(&config,&snapshot,0,true,0)==ESP_OK);
    assert(mappings==previous_mappings&&sd_configs==previous_configs&&wan_calls==previous_wan+1);
    assert(snapshot.live_count==2&&snapshot.clients.vpn==1&&snapshot.clients.total==2); /* no accumulation */
    mode=8;int64_t old_epoch=snapshot.wan_epoch;
    assert(unifi_api_poll(&config,&snapshot,0,true,0)!=ESP_OK);
    assert(snapshot.wan_valid&&snapshot.wan_epoch==old_epoch&&snapshot.wan_down_bps==800000&&snapshot.wan_error[0]);
    assert(!snapshot.clients.vpn_complete&&snapshot.clients.error[0]&&snapshot.sdwan.available);
    assert(connections==cleanups);
    /* Several independent SD-WANs are discovered incrementally by membership.
     * A third candidate cannot starve forever behind the first two probes. */
    mode=10;unifi_api_reset_cache();memset(&snapshot,0,sizeof snapshot);
    int configs_before=sd_configs,status_before=sd_status_calls;
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(!snapshot.sdwan.available&&strstr(snapshot.sdwan.error,"Discovering"));
    assert(sd_status_calls==status_before+2);
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(snapshot.sdwan.available&&snapshot.sdwan.connected==1);
    assert(sd_configs==configs_before+1&&sd_status_calls==status_before+3);
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(sd_configs==configs_before+1&&sd_status_calls==status_before+4);
    mode=11;unifi_api_reset_cache();memset(&snapshot,0,sizeof snapshot);
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(!snapshot.sdwan.available&&strstr(snapshot.sdwan.error,"Multiple matching"));
    mode=12;unifi_api_reset_cache();memset(&snapshot,0,sizeof snapshot);
    assert(unifi_api_poll(&config,&snapshot,0,false,0)!=ESP_OK);
    assert(!snapshot.sdwan.available&&strstr(snapshot.sdwan.error,"incomplete"));
    mode=13;unifi_api_reset_cache();memset(&snapshot,0,sizeof snapshot);
    assert(unifi_api_poll(&config,&snapshot,0,false,0)==ESP_OK);
    assert(!snapshot.sdwan.available&&strstr(snapshot.sdwan.error,"no matching"));
    puts("UniFi transport mocks: HTTPS verification, GET-only, site discovery/missing selection/empty list, 429 cooldown, cached 403 fallback, LAN without cloud PASS");return 0;
}
