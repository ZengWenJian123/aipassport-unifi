#include "unifi_api_parse.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unifi_parse_t parser;
static unifi_snapshot_t snap;
static int64_t now;
static bool feed(const char *s,size_t chunk){
    size_t len=strlen(s);for(size_t i=0;i<len;i+=chunk)if(!unifi_parse_feed(&parser,s+i,len-i<chunk?len-i:chunk))return false;
    return unifi_parse_finish(&parser);
}
static void init(unifi_parse_kind_t kind){memset(&snap,0,sizeof(snap));unifi_parse_init(&parser,kind,&snap,"host-a","site-a","default",now);}
static void test_json(void){
    const char *valid[]={"{}","[]","{\"x\":[1,0,-2.5,1e2,true,false,null,\"a\\n中\\u6587\\ud83d\\ude00\"]}","{\"a\":{\"a\":1},\"b\":[{},{}]}"," 123 "};
    const char *invalid[]={"{","[1,]","{\"a\":1,}","{\"a\" 1}","[01]","[.5]","[1e]","true false","{\"a\":\"\\ud800x\"}","{\"a\":\"\\udc00\"}","{\"a\":\"\\u0000\"}","{\"a\":\"\\x\"}"};
    for(size_t i=0;i<sizeof(valid)/sizeof(*valid);i++){ujson_stream_t s;ujson_init(&s,NULL,NULL);for(size_t j=0;j<strlen(valid[i]);j++)assert(ujson_feed(&s,valid[i]+j,1));assert(ujson_finish(&s));}
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++){ujson_stream_t s;ujson_init(&s,NULL,NULL);bool ok=ujson_feed(&s,invalid[i],strlen(invalid[i]));assert(!ok||!ujson_finish(&s));}
    char dst[6];unifi_copy_utf8(dst,sizeof(dst),"中文");assert(!strcmp(dst,"中"));
    assert(unifi_parse_time("2026-11-08T08:30:00+08:00")==now);
    assert(!unifi_parse_time("2026-02-29T00:00:00Z"));
    assert(!unifi_parse_time("2026-11-08T00:30:00"));
    assert(!unifi_parse_time("2026-11-08T-1:30:00Z"));
}
static void test_host_names(void){
    init(UNIFI_PARSE_SITES);
    assert(feed("{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"meta\":{\"name\":\"default\",\"desc\":\"Default\"}},{\"hostId\":\"host-b\",\"siteId\":\"site-b\",\"meta\":{\"name\":\"default\"}},{\"hostId\":\"host-a\",\"siteId\":\"site-c\",\"meta\":{\"desc\":\"Lab\"}}]}",3));
    unifi_parse_init(&parser,UNIFI_PARSE_HOSTS,&snap,NULL,NULL,NULL,now);
    assert(feed("{\"data\":[{\"reportedState\":{\"hostname\":\"old-name\",\"name\":\"Example Office\"},\"id\":\"host-a\"},{\"id\":\"host-b\",\"reportedState\":{\"hostname\":\"Example Home\"}},{\"id\":\"other\",\"reportedState\":null}]}",1));
    assert(!strcmp(snap.sites[0].name,"Example Office"));
    assert(!strcmp(snap.sites[1].name,"Example Home"));
    assert(!strcmp(snap.sites[2].name,"Lab"));
}
static void test_sites(void){
    init(UNIFI_PARSE_SITES);
    assert(feed("{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"meta\":{\"name\":\"default\",\"desc\":\"测试站点\"},\"statistics\":{\"counts\":{\"wifiClient\":51,\"wiredClient\":38,\"guestClient\":4,\"totalDevice\":11,\"offlineDevice\":1},\"percentages\":{\"wanUptime\":99.9}}}]}",3));
    assert(parser.matched&&snap.site_count==1&&snap.sites[0].clients==89&&snap.sites[0].devices_online==10);
    assert(!strcmp(snap.sites[0].name,"测试站点")&&!strcmp(parser.selected_reference,"default"));
    unifi_parse_init(&parser,UNIFI_PARSE_NETWORK_SITES,&snap,"host-a","site-a","default",now);
    assert(feed("{\"data\":[{\"id\":\"uuid-wrong\",\"internalReference\":\"other\"},{\"id\":\"uuid-right\",\"internalReference\":\"default\"}]}",1));
    assert(parser.matched&&!strcmp(snap.sites[0].network_site_id,"uuid-right"));
    init(UNIFI_PARSE_SITES);assert(feed("{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"statistics\":{\"ispInfo\":{\"name\":\"Example ISP\",\"organization\":\"Example Org\"},\"internetIssues\":[{\"type\":\"latency\"},{\"type\":\"packet-loss\"}]}}]}",3));
    assert(!strcmp(snap.sites[0].isp_name,"Example ISP")&&!strcmp(snap.sites[0].isp_organization,"Example Org"));
    assert(snap.sites[0].internet_issues_valid&&snap.sites[0].internet_issues==2&&!strcmp(snap.sites[0].status,"unknown"));
    init(UNIFI_PARSE_SITES);assert(feed("{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\"}]}",5));assert(!snap.sites[0].counts_valid&&snap.sites[0].clients==-1&&!snap.sites[0].internet_issues_valid);
}
static void test_devices(void){
    init(UNIFI_PARSE_DEVICES);
    assert(feed("{\"data\":[{\"devices\":[{\"id\":\"a\",\"name\":\"网关\",\"status\":\"online\",\"startupTime\":\"2026-11-07T00:30:00Z\"},{\"id\":\"b\",\"status\":\"offline\"}],\"hostId\":\"host-a\"},{\"hostId\":\"other\",\"devices\":[{\"id\":\"other\",\"status\":\"online\"}]}]}",7));
    assert(parser.matched&&snap.device_count==2&&snap.device_total==2&&snap.device_offline==1&&snap.devices[0].uptime_sec==86400);
    unifi_parse_init(&parser,UNIFI_PARSE_DEVICE_STATS,&snap,NULL,NULL,NULL,now);parser.target_device=0;
    assert(feed("{\"uptimeSec\":123,\"cpuUtilizationPct\":19.5,\"memoryUtilizationPct\":44,\"uplink\":{\"rxRateBps\":123456,\"txRateBps\":987},\"interfaces\":{}}",2));
    assert(snap.devices[0].rates_valid&&snap.devices[0].rx_bps==123456&&snap.devices[0].cpu_valid&&snap.devices[0].uptime_sec==123);
    unifi_parse_init(&parser,UNIFI_PARSE_DEVICE_STATS,&snap,NULL,NULL,NULL,now);assert(feed("{\"interfaces\":{}}",20));assert(!snap.devices[0].rates_valid&&!snap.devices[0].cpu_valid);
    unifi_parse_init(&parser,UNIFI_PARSE_DEVICE_STATS,&snap,NULL,NULL,NULL,now);assert(feed("{\"uptimeSec\":1e300}",1));assert(!snap.devices[0].uptime_valid);
}
static void test_clients(void){
    init(UNIFI_PARSE_CLIENTS);parser.vpn_only=true;parser.selected_offset=0;
    assert(feed("{\"totalCount\":5,\"data\":[{\"id\":\"a\",\"name\":\"无线\",\"type\":\"WIRELESS\",\"access\":{\"type\":\"GUEST\"}},{\"id\":\"b\",\"type\":\"WIRED\"},{\"id\":\"c\",\"name\":\"VPN旧\",\"type\":\"VPN\",\"ipAddress\":\"192.168.13.1\",\"connectedAt\":\"2026-11-07T22:30:00Z\"}]}",1));
    assert(snap.clients.total==3&&snap.clients.guest==1&&snap.clients.wireless==1&&snap.clients.returned==1);
    unifi_parse_next_client_page(&parser);
    assert(feed("{\"totalCount\":5,\"data\":[{\"id\":\"d\",\"type\":\"TELEPORT\"},{\"id\":\"e\",\"type\":\"VPN\",\"name\":\"最新VPN\",\"ipAddress\":\"192.168.13.2\",\"connectedAt\":\"2026-11-08T00:20:00Z\"}]}",13));
    assert(snap.clients.total==5&&snap.clients.vpn==2&&snap.clients.teleport==1&&snap.clients.guest==1&&snap.clients.returned==3);
    assert(!strcmp(snap.clients.items[1].type,"TELEPORT"));
    assert(snap.clients.latest_vpn_valid&&!strcmp(snap.clients.latest_vpn.name,"最新VPN"));
    unifi_parse_next_client_page(&parser);assert(feed("{\"totalCount\":5,\"data\":[{\"id\":\"e\",\"type\":\"VPN\"}]}",13));assert(parser.truncated&&snap.clients.vpn==2);
}
static void test_metrics(unifi_parse_kind_t kind){
    init(kind);char *json=calloc(200000,1);assert(json);size_t used=0;
    used+=(size_t)snprintf(json+used,200000-used,"{\"data\":[{\"periods\":[");
    int count=kind==UNIFI_PARSE_METRICS_5M?288:168,step=kind==UNIFI_PARSE_METRICS_5M?300:3600;
    int64_t end=now/step*step;
    for(int i=0;i<count;i++){
        time_t epoch=(time_t)(end-(count-1-i)*step);struct tm *t=gmtime(&epoch);char stamp[32];strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",t);
        used+=(size_t)snprintf(json+used,200000-used,"%s{\"data\":{\"wan\":{\"avgLatency\":7,\"maxLatency\":20,\"packetLoss\":0.5,\"download_kbps\":800,\"upload_kbps\":200,\"uptime\":100}},\"metricTime\":\"%s\"}",i?",":"",stamp);
    }
    used+=(size_t)snprintf(json+used,200000-used,"],\"hostId\":\"host-a\",\"siteId\":\"site-a\"},{\"hostId\":\"wrong\",\"siteId\":\"site-a\",\"periods\":[]}]}");
    assert(used>8192);assert(feed(json,17));assert(parser.matched);
    if(kind==UNIFI_PARSE_METRICS_5M){
        assert(snap.metrics_valid&&snap.metric_epoch==end&&snap.down_bps==800000&&snap.avg_latency_ms==7&&snap.loss_pct==.5&&snap.history_count==48);
        for(unsigned i=0;i<UNIFI_HISTORY_POINTS;i++){
            assert(snap.history_down[i]==800000&&snap.history_up[i]==200000);
            assert(snap.history_latency[i]==7&&snap.history_loss[i]==.5f);
        }
    }
    else{assert(!snap.days[0].valid&&!snap.days[6].valid&&snap.days[0].down_bytes==0);}
    free(json);
}
static void test_metric_history_gaps(void){
    init(UNIFI_PARSE_METRICS_5M);
    /* Within a half-hour bucket, each series counts only its available unique
     * five-minute samples. A duplicate may fill an absent field, never reweight it. */
    const char *json="{\"data\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"periods\":["
        "{\"metricTime\":\"2026-11-07T22:30:00Z\",\"data\":{\"wan\":{\"packetLoss\":0}}},"
        "{\"metricTime\":\"2026-11-07T23:00:00Z\",\"data\":{\"wan\":{\"avgLatency\":8}}},"
        "{\"metricTime\":\"2026-11-08T00:00:00Z\",\"data\":{\"wan\":{\"avgLatency\":10,\"packetLoss\":1,\"download_kbps\":100,\"upload_kbps\":20}}},"
        "{\"metricTime\":\"2026-11-08T00:05:00Z\",\"data\":{\"wan\":{\"avgLatency\":20,\"packetLoss\":null,\"download_kbps\":300,\"upload_kbps\":60}}},"
        "{\"metricTime\":\"2026-11-08T00:05:00Z\",\"data\":{\"wan\":{\"avgLatency\":999,\"packetLoss\":3,\"download_kbps\":999,\"upload_kbps\":999}}},"
        "{\"metricTime\":\"2026-11-08T00:10:00Z\",\"data\":{\"wan\":{\"avgLatency\":null,\"packetLoss\":5}}},"
        "{\"metricTime\":\"2026-11-08T00:15:00Z\",\"data\":{\"wan\":{\"avgLatency\":null,\"packetLoss\":null,\"download_kbps\":500,\"upload_kbps\":100}}},"
        "{\"metricTime\":\"2026-11-08T00:20:00Z\",\"data\":{\"wan\":{\"avgLatency\":30,\"download_kbps\":null,\"upload_kbps\":100}}},"
        "{\"metricTime\":\"2026-11-08T00:25:00Z\",\"data\":{\"wan\":{\"avgLatency\":-1,\"packetLoss\":-1}}},"
        "{\"metricTime\":\"2026-11-08T00:30:00Z\",\"data\":{\"wan\":{\"avgLatency\":7,\"maxLatency\":20,\"packetLoss\":0,\"download_kbps\":800,\"upload_kbps\":200}}}"
        "]}]}";
    assert(feed(json,1));
    assert(snap.metrics_valid&&snap.history_count==UNIFI_HISTORY_POINTS);
    for(unsigned i=0;i<UNIFI_HISTORY_POINTS;i++){
        if(i<43||i==45){
            assert(isnan(snap.history_down[i])&&isnan(snap.history_up[i]));
            assert(isnan(snap.history_latency[i])&&isnan(snap.history_loss[i]));
        }
    }
    assert(isnan(snap.history_latency[43])&&snap.history_loss[43]==0);
    assert(snap.history_latency[44]==8&&isnan(snap.history_loss[44]));
    assert(isnan(snap.history_down[43])&&isnan(snap.history_up[43]));
    assert(isnan(snap.history_down[44])&&isnan(snap.history_up[44]));
    assert(snap.history_down[46]==300000&&snap.history_up[46]==60000);
    assert(snap.history_latency[46]==20&&snap.history_loss[46]==3);
    assert(snap.history_latency[47]==7&&snap.history_loss[47]==0);
    assert(snap.history_down[47]==800000&&snap.history_up[47]==200000);
}
static void test_sdwan(void){
    init(UNIFI_PARSE_SDWAN_CONFIG);assert(feed("{\"data\":[{\"id\":\"config-a\",\"type\":\"sdwan-hbsp\"}]}",5));assert(!strcmp(parser.config_id,"config-a"));
    init(UNIFI_PARSE_SDWAN_STATUS);
    assert(feed("{\"data\":{\"hubs\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"primaryWanStatus\":{\"wanId\":\"WAN1\"},\"errors\":[]}],\"spokes\":[{\"hostId\":\"host-b\",\"connections\":[{\"tunnels\":[{\"status\":\"connected\"},{\"status\":\"pending\"}]}]}],\"warnings\":[{\"type\":\"sample\"}]}}",4));
    assert(snap.sdwan.available&&snap.sdwan.total==2&&snap.sdwan.connected==1&&snap.sdwan.hubs==1&&snap.sdwan.spokes==1&&snap.sdwan.warnings==1);
    init(UNIFI_PARSE_SDWAN_STATUS);assert(feed("{\"data\":{\"mesh\":[]}}",4));assert(!snap.sdwan.available&&snap.sdwan.error[0]);
}

static void test_wan_links(void){
    const char *dual="{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"last_wan_status\":{\"WAN2\":\"offline\",\"WAN\":\"online\"},\"wan2\":{\"up\":false,\"enable\":true},\"wan1\":{\"up\":true,\"enable\":true,\"is_uplink\":true},\"type\":\"udm\",\"mac\":\"host-a\"}]}";
    init(UNIFI_PARSE_WAN_LINKS);assert(feed(dual,1));
    assert(snap.wans[0].present&&snap.wans[1].present&&!snap.wans[2].present&&snap.wans_complete);
    assert(!strcmp(snap.wans[0].state,"online")&&!strcmp(snap.wans[1].state,"offline"));
    assert(snap.wans[0].link_up&&snap.wans[0].active&&!snap.wans[1].link_up);
    init(UNIFI_PARSE_WAN_LINKS);
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"type\":\"udm\",\"mac\":\"host-a\",\"wan1\":{\"up\":true},\"last_wan_status/WAN\":\"online\",\"last_wan_status~1WAN\":\"online\",\"udapi_version\":{\"versionDetail\":{\"vendor/package\":\"1.0\"}}}]}",1));
    assert(snap.wans[0].present&&!snap.wans[0].state[0]); // '/' is a key, not a nested path
    /* A successful re-discovery removes vanished WAN2, regardless of key order. */
    unifi_parse_init(&parser,UNIFI_PARSE_WAN_LINKS,&snap,"host-a",NULL,NULL,now);
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"mac\":\"host-a\",\"type\":\"uxg\",\"wan1\":{\"enable\":false},\"last_wan_interfaces\":{\"WAN\":{\"alive\":false}}}]}",7));
    assert(snap.wans[0].present&&!snap.wans[1].present&&!snap.wans[0].enabled&&!snap.wans[0].link_known);
    assert(!strcmp(snap.wans[0].state,"offline"));
    init(UNIFI_PARSE_WAN_LINKS);
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"type\":\"ugw\",\"mac\":\"host-a\",\"wan1\":{\"up\":true,\"availability\":100},\"wan8\":{},\"wan9\":{},\"port_table\":[{\"up\":true}]}]}",3));
    assert(snap.wans[0].present&&!snap.wans[0].state[0]); // carrier/uptime != Internet online
    assert(snap.wans[7].present&&!snap.wans_complete);
    init(UNIFI_PARSE_WAN_LINKS);assert(!feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"type\":\"uap\",\"mac\":\"host-a\",\"wan1\":{}}]}",3));
    init(UNIFI_PARSE_WAN_LINKS);assert(!feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"type\":\"udm\",\"mac\":\"other\",\"wan1\":{}}]}",3));
    init(UNIFI_PARSE_WAN_LINKS);assert(!feed("{\"meta\":{\"rc\":\"error\"},\"data\":[]}",3));
    init(UNIFI_PARSE_WAN_LINKS);assert(!feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"type\":\"udm\",\"mac\":\"host-a\"}]}",3));
    init(UNIFI_PARSE_WAN);
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"uptime_stats\":{\"WAN2\":{\"availability\":99.5,\"time_period\":86400},\"WAN\":{\"availability\":100,\"time_period\":7200}},\"gw_mac\":\"host-a\",\"rx_bytes-r\":0,\"tx_bytes-r\":0,\"subsystem\":\"wan\"},{\"subsystem\":\"wlan\",\"uptime_stats\":{\"WAN\":{\"availability\":12}}}]}",1));
    assert(snap.wans[0].availability_pct==100&&snap.wans[1].availability_pct==99.5&&snap.wans[0].period_sec==7200);
    assert(!strcmp(snap.wan_gateway_mac,"host-a")&&!snap.wans[0].present);
    unifi_parse_init(&parser,UNIFI_PARSE_WAN_LINKS,&snap,"host-a",NULL,NULL,now);assert(feed(dual,4));
    assert(snap.wans[1].availability_pct==99.5&&snap.wans[1].history_epoch==now);
}
static void test_current_wan(void){
    init(UNIFI_PARSE_WAN);
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"rx_bytes-r\":100000,\"tx_bytes-r\":12500,\"subsystem\":\"wan\"},{\"subsystem\":\"www\",\"rx_bytes-r\":999999,\"tx_bytes-r\":99999}]}",1));
    assert(parser.matched&&snap.wan_down_bps==800000&&snap.wan_up_bps==100000);
    init(UNIFI_PARSE_WAN);assert(!feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"subsystem\":\"wan\",\"rx_bytes-r\":null,\"tx_bytes-r\":0}]}",3));
    init(UNIFI_PARSE_WAN);assert(!feed("{\"meta\":{\"rc\":\"error\"},\"data\":[]}",3));
    init(UNIFI_PARSE_WAN);assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"subsystem\":\"wan\",\"rx_bytes-r\":0,\"tx_bytes-r\":0}]}",3));
    assert(parser.matched&&snap.wan_down_bps==0);
}
static void test_remote_vpn(void){
    init(UNIFI_PARSE_VPN);parser.vpn_only=true;
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"id\":\"one\",\"user\":\"Office laptop\",\"tunnel_ip\":\"192.0.2.10\",\"up\":true,\"vpn_type\":\"wireguard-server\",\"uptime\":120},{\"id\":\"offline\",\"up\":false,\"vpn_type\":\"wireguard-server\"},{\"id\":\"two\",\"user\":\"Home laptop\",\"up\":true,\"vpn_type\":\"openvpn-server\",\"uptime\":10},{\"id\":\"three\",\"up\":true,\"vpn_type\":\"teleport\",\"uptime\":5}]}",1));
    assert(snap.clients.vpn==2&&snap.clients.teleport==1&&snap.clients.returned==3);
    assert(snap.clients.latest_vpn.connected_epoch==now-10&&!strcmp(snap.clients.latest_vpn.name,"Home laptop"));
    assert(!strcmp(snap.clients.items[0].ip,"192.0.2.10"));
    init(UNIFI_PARSE_VPN);assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[]}",1));assert(!snap.clients.vpn);
    init(UNIFI_PARSE_VPN);assert(!feed("{\"meta\":{\"rc\":\"error\"},\"data\":[]}",1));
    init(UNIFI_PARSE_VPN);parser.vpn_only=true;parser.selected_offset=1;
    assert(feed("{\"meta\":{\"rc\":\"ok\"},\"data\":[{\"id\":\"a\",\"up\":true,\"vpn_type\":\"wireguard-server\",\"uptime\":1e300},{\"id\":\"b\",\"up\":true,\"vpn_type\":\"wireguard-server\",\"uptime\":100},{\"id\":\"b\",\"up\":true,\"vpn_type\":\"wireguard-server\"}]}",2));
    assert(parser.truncated&&snap.clients.vpn==2&&snap.clients.returned==1&&snap.clients.items[0].connected_epoch==now-100);
}
static void test_mesh(void){
    const char *json="{\"data\":{\"status\":{\"connections\":[{\"sites\":[{\"hostId\":\"other\",\"siteId\":\"remote\",\"status\":{\"connected\":true,\"offline\":false,\"ping\":0}},{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"status\":{\"connected\":true,\"offline\":false}}]}],\"sites\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"warnings\":[\"warning\"],\"errors\":[]},{\"hostId\":\"other\",\"siteId\":\"remote\",\"errors\":[]}]}}}";
    init(UNIFI_PARSE_SDWAN_STATUS);assert(feed(json,1));assert(snap.sdwan.available&&snap.sdwan.connected==1&&snap.sdwan.total==1&&snap.sdwan.peers==2&&snap.sdwan.warnings==1);
    assert(!strcmp(snap.sdwan.remote_host,"other")&&!strcmp(snap.sdwan.primary,"unknown"));
    init(UNIFI_PARSE_SDWAN_STATUS);strcpy(parser.wanted_site,"another");assert(feed(json,7));assert(!snap.sdwan.available&&snap.sdwan.total==0);
    init(UNIFI_PARSE_SDWAN_STATUS);assert(feed("{\"data\":{\"status\":{\"connections\":[{\"sites\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"status\":{\"connected\":true}},{\"hostId\":\"b\",\"status\":{\"connected\":true,\"offline\":false}}]}]}}}",3));assert(!snap.sdwan.available&&!snap.sdwan.connected);
    init(UNIFI_PARSE_SDWAN_STATUS);assert(feed("{\"data\":{\"status\":{\"connections\":[{\"sites\":[{\"hostId\":\"host-a\",\"siteId\":\"site-a\",\"status\":{\"connected\":true,\"offline\":false}},{\"hostId\":\"b\",\"status\":{\"connected\":true,\"offline\":true}}]}]}}}",3));assert(snap.sdwan.available&&!snap.sdwan.connected&&snap.sdwan.total==1);
}

int main(void){
    now=unifi_parse_time("2026-11-08T00:30:00Z");assert(now>0);
    test_wan_links();test_current_wan();test_remote_vpn();test_mesh();test_json();test_sites();test_host_names();test_devices();test_clients();test_metrics(UNIFI_PARSE_METRICS_5M);test_metrics(UNIFI_PARSE_METRICS_1H);test_metric_history_gaps();test_sdwan();
    printf("UniFi parser: JSON fragmentation, >8KB ISP, sites, devices, clients/VPN, SD-WAN PASS (JSON state %zu bytes, parse state %zu bytes)\n",sizeof(ujson_stream_t),sizeof(unifi_parse_t));return 0;
}
