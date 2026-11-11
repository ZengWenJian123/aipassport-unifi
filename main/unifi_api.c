#include "unifi_api.h"
#include "unifi_api_parse.h"
#include "unifi_demo.h"
#include "unifi_site_names.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define COPY(d,s) unifi_copy_utf8((d),sizeof(d),(s))
#define RESPONSE_LIMIT (1024u * 1024u)
#define CLIENT_PAGES 3
#define CLIENT_BATCH 100
/* Worker-owned cache timing. No key, password or response bodies are logged. */
static int64_t s_retry_until, s_metrics_at, s_hosts_at, s_mapping_at, s_vpn_retry_at;
static char s_cache_host[96], s_cache_site[64], s_reference[64];
static bool s_local;
static unsigned s_maintenance;
static char s_sdwan_id[64],s_sdwan_type[24];
static int64_t s_sdwan_at;
static unifi_sdwan_config_t s_sd_candidates[UNIFI_SDWAN_CONFIG_MAX];
static unsigned s_sd_count,s_sd_cursor,s_sd_matches;
static bool s_sd_scanning,s_sd_incomplete;
static unifi_sdwan_t s_sd_match_status;
static void reset_sdwan(void){
    s_sdwan_at=0;s_sdwan_id[0]=s_sdwan_type[0]=0;
    s_sd_count=s_sd_cursor=s_sd_matches=0;s_sd_scanning=s_sd_incomplete=false;
    memset(s_sd_candidates,0,sizeof s_sd_candidates);memset(&s_sd_match_status,0,sizeof s_sd_match_status);
}
void unifi_api_reset_cache(void){s_metrics_at=s_hosts_at=s_mapping_at=s_vpn_retry_at=0;s_maintenance=0;reset_sdwan();s_cache_host[0]=s_cache_site[0]=s_reference[0]=0;s_local=false;}
typedef struct { unsigned retry; bool close; } response_headers_t;
/* One worker-owned session per poll, never shared with the portal. */
typedef struct { esp_http_client_handle_t client; response_headers_t headers; unifi_api_progress_cb progress; void *context; unsigned count; } http_session_t;
static void close_session(http_session_t *session){
    if(session->client){esp_http_client_close(session->client);esp_http_client_cleanup(session->client);session->client=NULL;}
}
static esp_err_t header_event(esp_http_client_event_t *event){
    response_headers_t *h=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value && !strcasecmp(event->header_key,"Retry-After")){
        char *end=NULL;unsigned long seconds=strtoul(event->header_value,&end,10);
        if(end && !*end && seconds>0)h->retry=seconds>3600?3600:(unsigned)seconds;
    }
    if(event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"Connection")&&!strcasecmp(event->header_value,"close"))h->close=true;
    return ESP_OK;
}
static int64_t monotonic_sec(void){return esp_timer_get_time()/1000000;}
static bool safe_segment(const char *s){
    if(!s[0])return false;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'||*s==':'))return false;
    return true;
}
static esp_err_t get_json_once(const unifi_config_t *cfg,const char *url,unifi_parse_t *parse,unifi_snapshot_t *status,http_session_t *session){
    status->http_status=0;
    session->headers.retry=30;session->headers.close=false;
    esp_http_client_config_t hc={.url=url,.timeout_ms=6500,.method=HTTP_METHOD_GET,
        .disable_auto_redirect=true,.event_handler=header_event,.user_data=&session->headers,
        .buffer_size=1024,.buffer_size_tx=1024,.crt_bundle_attach=esp_crt_bundle_attach};
    if(cfg->local_api && cfg->ca_pem[0] && strncmp(url,"https://api.ui.com/",19)){hc.cert_pem=cfg->ca_pem;hc.crt_bundle_attach=NULL;}
    if(strncmp(url,"https://",8))return ESP_ERR_INVALID_ARG;
    if(session->client && esp_http_client_set_url(session->client,url)!=ESP_OK)close_session(session);
    if(!session->client)session->client=esp_http_client_init(&hc);
    esp_http_client_handle_t client=session->client;if(!client)return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client,"Accept","application/json");
    esp_http_client_set_header(client,"X-API-KEY",cfg->api_key);
    esp_err_t err=esp_http_client_open(client,0);
    if(err!=ESP_OK)goto done;
    int64_t length=esp_http_client_fetch_headers(client);
    int code=esp_http_client_get_status_code(client);status->http_status=code;
    if(code==429){status->retry_after_sec=session->headers.retry;s_retry_until=monotonic_sec()+session->headers.retry;err=ESP_ERR_TIMEOUT;goto done;}
    if(code!=200){err=ESP_FAIL;goto done;}
    if(length>(int64_t)RESPONSE_LIMIT){err=ESP_ERR_INVALID_SIZE;goto done;}
    size_t total=0;char chunk[768];int64_t start=monotonic_sec();
    for(;;){
        int n=esp_http_client_read(client,chunk,sizeof(chunk));
        if(n<0){err=ESP_FAIL;break;}
        if(!n){if(!esp_http_client_is_complete_data_received(client)){err=ESP_ERR_TIMEOUT;}else if(!unifi_parse_finish(parse)){err=ESP_ERR_INVALID_RESPONSE;}break;}
        total+=(size_t)n;
        if(total>RESPONSE_LIMIT || monotonic_sec()-start>12){err=ESP_ERR_INVALID_SIZE;break;}
        if(!unifi_parse_feed(parse,chunk,(size_t)n)){err=ESP_ERR_INVALID_RESPONSE;break;}
    }
done:
    if(err!=ESP_OK||session->headers.close)close_session(session);
    return err;
}
/* Retry one connection failure before any HTTP response/body, including cold TLS.
 * Status errors (especially 429) and partial bodies are never retried here. */
static esp_err_t get_json(const unifi_config_t *cfg,const char *url,unifi_parse_t *parse,unifi_snapshot_t *status,http_session_t *session){
    static const char *stages[]={"Console names","Sites","Network site mapping","Devices","Network devices","Device statistics","Clients","ISP metrics","7-day estimates","SD-WAN","SD-WAN status","Current WAN rates","VPN sessions","WAN link status"};
    int64_t start=esp_timer_get_time();
    esp_err_t err=get_json_once(cfg,url,parse,status,session);
    if(err!=ESP_OK&&status->http_status<=0&&err!=ESP_ERR_NO_MEM&&err!=ESP_ERR_INVALID_ARG&&esp_timer_get_time()-start<12000000)
        err=get_json_once(cfg,url,parse,status,session);
    COPY(status->query_stage,stages[parse->kind]);status->query_count=++session->count;
    status->query_duration_ms=(uint32_t)((esp_timer_get_time()-start)/1000);status->query_http=status->http_status;
    if(session->progress&&parse->kind!=UNIFI_PARSE_SITES&&parse->kind!=UNIFI_PARSE_NETWORK_SITES){
        status->refreshing=true;session->progress(status,session->context);status->refreshing=false;
    }
    if(err!=ESP_OK)ESP_LOGW("unifi_api","endpoint=%d HTTP=%d error=%s",(int)parse->kind,status->http_status,esp_err_to_name(err));
    return err;
}
static bool network_base(const unifi_config_t *c,const char *host,char *dst,size_t n){
    if(c->local_api){
        if(strncmp(c->console_url,"https://",8)||strchr(c->console_url+8,'@')||strchr(c->console_url,'?')||strchr(c->console_url,'#'))return false;
        size_t len=strlen(c->console_url);while(len&&c->console_url[len-1]=='/')len--;
        int written=snprintf(dst,n,"%.*s%s",(int)len,c->console_url,strstr(c->console_url,"/proxy/network/integration")?"":"/proxy/network/integration");
        return written>0&&(size_t)written<n;
    }
    if(!safe_segment(host))return false;
    int written=snprintf(dst,n,"https://api.ui.com/v1/connector/consoles/%s/proxy/network/integration",host);
    return written>0&&(size_t)written<n;
}
static void record_failure(unifi_snapshot_t *s,const char *source,esp_err_t err){
    s->api_ok=false;
    if(s->http_status==429)snprintf(s->error,sizeof(s->error),"%s: HTTP 429; retry %lus",source,(unsigned long)s->retry_after_sec);
    else if(s->http_status>=300)snprintf(s->error,sizeof(s->error),"%s: HTTP %d",source,s->http_status);
    else snprintf(s->error,sizeof(s->error),"%s: %s",source,esp_err_to_name(err));
}
static bool budget(int64_t begin){return monotonic_sec()-begin<35 && monotonic_sec()>=s_retry_until;}
static void publish_progress(unifi_snapshot_t *s,unifi_api_progress_cb progress,void *context){
    if(!progress)return;
    s->refreshing=true;progress(s,context);s->refreshing=false;
}
static void connection_result(unifi_snapshot_t *s,esp_err_t err,int64_t now){
    s->connection_tested=true;s->connection_ok=err==ESP_OK;
    s->connection_http=s->http_status;s->connection_epoch=now;
    s->connection_error[0]=0;
    if(err!=ESP_OK){record_failure(s,"Connection test",err);COPY(s->connection_error,s->error);}
}
esp_err_t unifi_api_poll_with_progress(const unifi_config_t *cfg,unifi_snapshot_t *s,uint32_t client_offset,bool vpn_only,int device_index,unifi_api_progress_cb progress,void *context){
    if(!cfg||!s)return ESP_ERR_INVALID_ARG;
    if(cfg->demo){unifi_api_demo(s,client_offset,vpn_only);return ESP_OK;}
    s->demo=false;s->refreshing=false;s->retry_after_sec=0;s->http_status=0;
    if(!cfg->api_key[0]){s->api_ok=false;COPY(s->error,"API key not configured");return ESP_ERR_INVALID_STATE;}
    int64_t now=time(NULL),begin=monotonic_sec();
    if(now<1700000000){s->api_ok=false;COPY(s->error,"Waiting for valid clock (HTTPS)");return ESP_ERR_INVALID_STATE;}
    if(strcmp(s_cache_host,cfg->host_id)||strcmp(s_cache_site,cfg->site_id)||s_local!=cfg->local_api){
        COPY(s_cache_host,cfg->host_id);COPY(s_cache_site,cfg->site_id);s_local=cfg->local_api;s_metrics_at=s_hosts_at=s_mapping_at=s_vpn_retry_at=0;s_maintenance=0;reset_sdwan();s_reference[0]=0;
        memset(s,0,sizeof(*s));
    }
    if(begin<s_retry_until){s->api_ok=false;s->http_status=429;s->retry_after_sec=(uint32_t)(s_retry_until-begin);COPY(s->error,"Rate limited; waiting before retry");return ESP_ERR_TIMEOUT;}
    unifi_snapshot_t *work=malloc(sizeof(*work));unifi_parse_t *p=malloc(sizeof(*p));
    if(!work||!p){free(work);free(p);s->api_ok=false;COPY(s->error,"Not enough memory for API read");return ESP_ERR_NO_MEM;}
    *work=*s;s->api_ok=true;s->error[0]=0;bool any=false;esp_err_t err;
    char url[480],base[320];http_session_t session={.progress=progress,.context=context};s->query_count=0;
    if(!cfg->local_api){
        /* Keep an automatically selected site stable when API ordering changes.
         * Otherwise old telemetry could be relabelled with the new first site. */
        const unifi_site_t *previous=s->selected_site<s->site_count?&s->sites[s->selected_site]:NULL;
        bool automatic=!cfg->host_id[0]&&!cfg->site_id[0]&&previous;
        unifi_parse_init(p,UNIFI_PARSE_SITES,work,
                        automatic?previous->host_id:cfg->host_id,
                        automatic?previous->site_id:cfg->site_id,NULL,now);
        err=get_json(cfg,"https://api.ui.com/v1/sites?pageSize=8",p,s,&session);
        connection_result(s,err,now);
        if(err!=ESP_OK){publish_progress(s,progress,context);if(s->http_status==401||s->http_status==403||s->http_status==429||s->selected_site>=s->site_count)goto finish;}
        if(err==ESP_OK){
            /* Discovery and selection are separate. An obsolete saved target
             * must not hide other accessible sites or attach its cached
             * telemetry to the first newly discovered site. */
            if(!p->matched){
                memset(s,0,sizeof(*s));s->http_status=200;s->selected_site=UINT8_MAX;
                connection_result(s,ESP_OK,now);
                s_metrics_at=s_hosts_at=s_mapping_at=s_vpn_retry_at=0;s_maintenance=0;reset_sdwan();s_reference[0]=0;
            }
            for(unsigned i=0;i<work->site_count;i++)for(unsigned j=0;j<s->site_count;j++){
                if(!strcmp(work->sites[i].host_id,s->sites[j].host_id)&&!strcmp(work->sites[i].site_id,s->sites[j].site_id)){
                    if(unifi_site_generic_name(work->sites[i].name)&&!unifi_site_generic_name(s->sites[j].name))COPY(work->sites[i].name,s->sites[j].name);
                    COPY(work->sites[i].network_site_id,s->sites[j].network_site_id);
                }
            }
            memcpy(s->sites,work->sites,sizeof(s->sites));s->site_count=work->site_count;s->fetched_epoch=now;any=true;
            bool selected=p->matched,sites_limited=p->truncated||p->next_token[0];
            if(selected)s->selected_site=work->selected_site;
            COPY(s_reference,p->selected_reference);
            s->fetched_epoch=now;publish_progress(s,progress,context);
            if(s->site_count&&budget(begin)&&(!s_hosts_at||now-s_hosts_at>=300)){
                *work=*s;unifi_parse_init(p,UNIFI_PARSE_HOSTS,work,NULL,NULL,NULL,now);
                err=get_json(cfg,"https://api.ui.com/v1/hosts?pageSize=8",p,s,&session);
                if(err==ESP_OK){for(unsigned i=0;i<s->site_count;i++)COPY(s->sites[i].name,work->sites[i].name);s_hosts_at=now;}
                else record_failure(s,"Console names",err);
            }
            if(!selected){
                COPY(s->error,s->site_count?"Selected site unavailable; choose a discovered site":"No sites available for this API key");
                publish_progress(s,progress,context);
                goto finish;
            }
            if(sites_limited){s->api_ok=false;COPY(s->error,"Site list limited; select a listed site");}
            publish_progress(s,progress,context);
        }
    }
    if(!s->site_count && cfg->local_api){s->site_count=1;s->selected_site=0;COPY(s->sites[0].name,"Local console");COPY(s->sites[0].site_id,cfg->site_id);COPY(s->sites[0].host_id,cfg->host_id);s->sites[0].clients=s->sites[0].devices_online=s->sites[0].devices_total=-1;}
    /* A later discovery failure can retain a list with no selected target.
     * Validate the sentinel before any per-site dereference or request. */
    if(!s->site_count||s->selected_site>=s->site_count||s->selected_site>=UNIFI_SITE_MAX||!budget(begin))goto finish;
    unifi_site_t *site=&s->sites[s->selected_site];
    *work=*s;
    if(!network_base(cfg,site->host_id,base,sizeof(base))){s->api_ok=false;COPY(s->error,"Invalid HTTPS console address or host ID");goto finish;}
    if(budget(begin)&&(cfg->local_api||!site->network_site_id[0]||now-s_mapping_at>=300)){
        *work=*s;unifi_parse_init(p,UNIFI_PARSE_NETWORK_SITES,work,site->host_id,cfg->site_id,s_reference,now);
        snprintf(url,sizeof(url),"%s/v1/sites?limit=25",base);err=get_json(cfg,url,p,s,&session);
        if(cfg->local_api)connection_result(s,err,now);
        if(err==ESP_OK&&p->matched){
            COPY(site->network_site_id,work->sites[work->selected_site].network_site_id);
            if(p->selected_reference[0])COPY(s_reference,p->selected_reference);
            s_mapping_at=now;
            s->fetched_epoch=now;
            if(cfg->local_api){COPY(site->name,work->sites[work->selected_site].name);publish_progress(s,progress,context);}
            any=true;
        }
        else{record_failure(s,"Network site mapping",err==ESP_OK?ESP_ERR_NOT_FOUND:err);COPY(s->clients.error,s->error);goto maintenance;}
    }
    /* Compact current WAN data first. The controller compatibility API is
     * version-dependent; missing fields/HTTP errors never become zero rates. */
    char controller[320];COPY(controller,base);
    char *integration=strstr(controller,"/integration");if(integration)*integration=0;
    if(safe_segment(s_reference)&&budget(begin)){
        *work=*s;unifi_parse_init(p,UNIFI_PARSE_WAN,work,NULL,NULL,NULL,time(NULL));
        snprintf(url,sizeof(url),"%s/api/s/%s/stat/health",controller,s_reference);
        err=get_json(cfg,url,p,s,&session);
        if(err==ESP_OK){
            s->wan_valid=true;s->wan_epoch=time(NULL);s->wan_error[0]=0;
            COPY(s->wan_gateway_mac,work->wan_gateway_mac);
            for(unsigned i=0;i<UNIFI_WAN_MAX;i++){
                s->wans[i].availability_pct=work->wans[i].availability_pct;
                s->wans[i].history_epoch=work->wans[i].history_epoch;s->wans[i].period_sec=work->wans[i].period_sec;
            }
            s->wan_down_bps=work->wan_down_bps;s->wan_up_bps=work->wan_up_bps;
            unsigned at=s->live_count;
            if(at==UNIFI_HISTORY_POINTS){
                memmove(s->live_down,s->live_down+1,(UNIFI_HISTORY_POINTS-1)*sizeof(float));
                memmove(s->live_up,s->live_up+1,(UNIFI_HISTORY_POINTS-1)*sizeof(float));
                memmove(s->live_epoch,s->live_epoch+1,(UNIFI_HISTORY_POINTS-1)*sizeof(int64_t));at--;
            }else s->live_count++;
            s->live_down[at]=(float)s->wan_down_bps;s->live_up[at]=(float)s->wan_up_bps;s->live_epoch[at]=s->wan_epoch;
            any=true;publish_progress(s,progress,context);
        }else{record_failure(s,"Current WAN",err);COPY(s->wan_error,s->error);}
    }
    /* Query only the gateway identified by stat/health, not every AP/switch.
     * A separate age/error prevents fresh aggregate rates masking stale links. */
    if(s->wan_gateway_mac[0]&&safe_segment(s->wan_gateway_mac)&&budget(begin)){
        *work=*s;unifi_parse_init(p,UNIFI_PARSE_WAN_LINKS,work,s->wan_gateway_mac,NULL,NULL,time(NULL));
        snprintf(url,sizeof(url),"%s/api/s/%s/stat/device/%s",controller,s_reference,s->wan_gateway_mac);
        err=get_json(cfg,url,p,s,&session);
        if(err==ESP_OK){memcpy(s->wans,work->wans,sizeof s->wans);s->wans_complete=work->wans_complete;s->wans_available=true;s->wans_epoch=time(NULL);s->wans_error[0]=0;}
        else{record_failure(s,"WAN links",err);COPY(s->wans_error,s->error);}
    }else COPY(s->wans_error,"WAN gateway unavailable or refresh budget reached");
    publish_progress(s,progress,context);
    if(cfg->local_api){s->sdwan.available=false;s->sdwan.total=s->sdwan.connected=-1;COPY(s->sdwan.error,"Local Network API does not expose Mesh status");}
    if(!cfg->local_api&&budget(begin)){
        *work=*s;
        bool probed=false;
        if(!s_sd_scanning&&(!s_sdwan_at||now-s_sdwan_at>=300)){
            unifi_parse_init(p,UNIFI_PARSE_SDWAN_CONFIG,work,NULL,NULL,NULL,now);
            err=get_json(cfg,"https://api.ui.com/v1/sd-wan-configs",p,s,&session);
            if(err==ESP_OK){
                reset_sdwan();s_sdwan_at=now;s_sd_count=p->config_count;
                memcpy(s_sd_candidates,p->configs,s_sd_count*sizeof s_sd_candidates[0]);
                s_sd_incomplete=p->truncated||p->next_token[0];s_sd_scanning=s_sd_count>0;
                s->sdwan.available=false;
                COPY(s->sdwan.error,s_sd_count?"Discovering SD-WAN membership":"No SD-WAN configurations");
            }else record_failure(s,"SD-WAN",err);
        }
        /* At most two probes per round; continue next round without blocking
         * clients/WAN for accounts containing several independent networks. */
        for(unsigned n=0;s_sd_scanning&&s_sd_cursor<s_sd_count&&n<2&&budget(begin);n++){
            const unifi_sdwan_config_t *candidate=&s_sd_candidates[s_sd_cursor++];
            if((strcmp(candidate->type,"sdwan-hbsp")&&strcmp(candidate->type,"s2s-vpn"))||!safe_segment(candidate->id)){
                s_sd_incomplete=true;continue;
            }
            *work=*s;
            snprintf(url,sizeof(url),"https://api.ui.com/v1/sd-wan-configs/%s/status",candidate->id);
            unifi_parse_init(p,UNIFI_PARSE_SDWAN_STATUS,work,site->host_id,site->site_id,NULL,now);
            err=get_json(cfg,url,p,s,&session);probed=true;
            if(err!=ESP_OK){s_sd_incomplete=true;record_failure(s,"SD-WAN discovery",err);continue;}
            if(p->sd_has_host){
                s_sd_matches++;
                if(s_sd_matches==1){COPY(s_sdwan_id,candidate->id);COPY(s_sdwan_type,candidate->type);s_sd_match_status=work->sdwan;}
            }
        }
        if(s_sd_scanning&&s_sd_cursor==s_sd_count){
            s_sd_scanning=false;
            if(s_sd_matches==1&&!s_sd_incomplete){
                s->sdwan=s_sd_match_status;COPY(s->sdwan.type,!strcmp(s_sdwan_type,"s2s-vpn")?"Mesh":"sdwan-hbsp");
                s->sdwan.updated_epoch=now;
            }else{
                s_sdwan_id[0]=0;s->sdwan.available=false;s->sdwan.connected=s->sdwan.total=-1;
                COPY(s->sdwan.error,s_sd_incomplete?"SD-WAN discovery incomplete":s_sd_matches>1?"Multiple matching SD-WAN configurations":"Site has no matching SD-WAN configuration");
            }
        }
        if(!s_sd_scanning&&s_sdwan_id[0]&&!probed&&budget(begin)){
            *work=*s;
            snprintf(url,sizeof(url),"https://api.ui.com/v1/sd-wan-configs/%s/status",s_sdwan_id);
            unifi_parse_init(p,UNIFI_PARSE_SDWAN_STATUS,work,site->host_id,site->site_id,NULL,now);err=get_json(cfg,url,p,s,&session);
            if(err==ESP_OK){s->sdwan=work->sdwan;COPY(s->sdwan.type,!strcmp(s_sdwan_type,"s2s-vpn")?"Mesh":"sdwan-hbsp");s->sdwan.updated_epoch=time(NULL);if(!p->sd_has_host)s_sdwan_at=0;}
            else{record_failure(s,"SD-WAN status",err);COPY(s->sdwan.error,"SD-WAN status API failed; cached data");if(s->http_status==404)s_sdwan_at=0;}
        }
        publish_progress(s,progress,context);
    }
    if(!safe_segment(site->network_site_id))goto maintenance;
    if(budget(begin)){
        *work=*s;memset(&work->clients,0,sizeof(work->clients));work->clients.page_offset=client_offset;
        unifi_parse_init(p,UNIFI_PARSE_CLIENTS,work,NULL,NULL,NULL,now);p->selected_offset=client_offset;p->vpn_only=vpn_only;
        bool complete=false,failed=false;unsigned expected=0;
        for(unsigned page=0;page<CLIENT_PAGES&&budget(begin);page++){
            unifi_parse_next_client_page(p);
            snprintf(url,sizeof(url),"%s/v1/sites/%s/clients?offset=%u&limit=%u",base,site->network_site_id,page*CLIENT_BATCH,CLIENT_BATCH);
            err=get_json(cfg,url,p,s,&session);
            if(err!=ESP_OK){failed=true;break;}
            if(page && expected!=p->total_count)p->truncated=true;
            expected=p->total_count;
            if(page*CLIENT_BATCH+p->rows>=expected){complete=!p->truncated;break;}
            if(p->rows<CLIENT_BATCH){p->truncated=true;break;}
        }
        if(failed){record_failure(s,"Clients",err);COPY(s->clients.error,s->error);s->clients.complete=false;s->clients.vpn_complete=false;}
        else{work->clients.available=true;work->clients.complete=complete;work->clients.vpn_complete=complete;if(!complete){COPY(work->clients.error,"Partial: scan limited to 300 clients / time budget");s->api_ok=false;COPY(s->error,work->clients.error);}s->clients=work->clients;s->clients_epoch=time(NULL);s->fetched_epoch=now;if(complete)site->clients=s->clients.total;any=true;}
    }
    /* Network 10.6 omits remote VPNs from integration clients. Query current
     * remote sessions only when the official list has no VPN/Teleport entries.
     * This supplements physical clients; never double-count two VPN sources. */
    if(s->clients.available&&!s->clients.vpn&&!s->clients.teleport){
        s->clients.vpn_complete=false;
        if(safe_segment(s_reference)&&budget(begin)&&now>=s_vpn_retry_at){
            *work=*s;
            if(vpn_only)work->clients.returned=0;
            unifi_parse_init(p,UNIFI_PARSE_VPN,work,NULL,NULL,NULL,time(NULL));p->vpn_only=vpn_only;
            p->selected_offset=vpn_only?client_offset:0;
            if(!vpn_only)p->selected_seen=(unsigned)(s->clients.total>0?s->clients.total:0);
            if(!vpn_only)p->selected_offset=client_offset;
            snprintf(url,sizeof(url),"%s/api/s/%s/stat/remoteuservpn",controller,s_reference);
            err=get_json(cfg,url,p,s,&session);
            if(err==ESP_OK){s->clients=work->clients;s->clients.vpn_complete=!p->truncated;s->clients_epoch=time(NULL);if(p->truncated)COPY(s->clients.error,"VPN session list incomplete");if(s->clients.complete)site->clients=s->clients.total;}
            else{record_failure(s,"VPN sessions",err);COPY(s->clients.error,s->error);if(s->http_status==404||s->http_status==403)s_vpn_retry_at=now+300;}
        }else COPY(s->clients.error,"VPN sessions unavailable or waiting for retry");
    }
    publish_progress(s,progress,context);
    if(s_maintenance%2==0&&!cfg->local_api&&budget(begin)){
        *work=*s;unifi_parse_init(p,UNIFI_PARSE_DEVICES,work,site->host_id,site->site_id,NULL,now);
        if(safe_segment(site->host_id))snprintf(url,sizeof(url),"https://api.ui.com/v1/devices?hostIds%%5B%%5D=%s&pageSize=8",site->host_id);
        else snprintf(url,sizeof(url),"https://api.ui.com/v1/devices?pageSize=8");
        err=get_json(cfg,url,p,s,&session);
        if(err==ESP_OK && p->matched){memcpy(s->devices,work->devices,sizeof(s->devices));s->device_count=work->device_count;s->device_total=work->device_total;s->device_offline=work->device_offline;s->devices_complete=work->devices_complete;any=true;}
        else record_failure(s,"Devices",err==ESP_OK?ESP_ERR_NOT_FOUND:err);
    }
    if(s_maintenance%2==0&&budget(begin)){
        *work=*s;unifi_parse_init(p,UNIFI_PARSE_NETWORK_DEVICES,work,NULL,NULL,NULL,now);
        snprintf(url,sizeof(url),"%s/v1/sites/%s/devices?limit=12",base,site->network_site_id);err=get_json(cfg,url,p,s,&session);
        if(err==ESP_OK){
            for(unsigned i=0;i<work->device_count;i++)for(unsigned j=0;j<s->device_count;j++)if(!strcmp(work->devices[i].name,s->devices[j].name)){work->devices[i].startup_epoch=s->devices[j].startup_epoch;work->devices[i].uptime_sec=s->devices[j].uptime_sec;work->devices[i].uptime_valid=s->devices[j].uptime_valid;break;}
            memcpy(s->devices,work->devices,sizeof(s->devices));s->device_count=work->device_count;s->devices_complete=work->devices_complete;
            if(work->devices_complete){s->device_total=work->device_total;s->device_offline=work->device_offline;site->devices_total=work->device_total;site->devices_online=work->device_total-work->device_offline;site->counts_valid=true;}
            int idx=device_index>=0&&device_index<s->device_count?device_index:0;
            if(s->device_count&&safe_segment(s->devices[idx].id)&&budget(begin)){
                *work=*s;unifi_parse_init(p,UNIFI_PARSE_DEVICE_STATS,work,NULL,NULL,NULL,now);p->target_device=idx;
                snprintf(url,sizeof(url),"%s/v1/sites/%s/devices/%s/statistics/latest",base,site->network_site_id,s->devices[idx].id);err=get_json(cfg,url,p,s,&session);
                if(err==ESP_OK)s->devices[idx]=work->devices[idx];else record_failure(s,"Device statistics",err);
            }
        }else record_failure(s,"Network devices",err);
    }
maintenance:
    if((s_maintenance%2==1||!s->metrics_valid)&&!cfg->local_api&&budget(begin)&&(now-s_metrics_at>=300 || !s->metrics_valid)){
        *work=*s;unifi_parse_init(p,UNIFI_PARSE_METRICS_5M,work,site->host_id,site->site_id,NULL,now);
        err=get_json(cfg,"https://api.ui.com/v1/isp-metrics/5m?duration=24h",p,s,&session);
        if(err==ESP_OK&&p->matched&&work->metrics_valid){s->metrics_valid=work->metrics_valid;s->metric_epoch=work->metric_epoch;s->avg_latency_ms=work->avg_latency_ms;s->max_latency_ms=work->max_latency_ms;s->loss_pct=work->loss_pct;s->down_bps=work->down_bps;s->up_bps=work->up_bps;s->history_count=work->history_count;memcpy(s->history_down,work->history_down,sizeof(s->history_down));memcpy(s->history_up,work->history_up,sizeof(s->history_up));memcpy(s->history_latency,work->history_latency,sizeof(s->history_latency));memcpy(s->history_loss,work->history_loss,sizeof(s->history_loss));s_metrics_at=now;any=true;}
        else record_failure(s,"ISP metrics",err==ESP_OK?ESP_ERR_NOT_FOUND:err);
    }
    publish_progress(s,progress,context);
    /* ISP download_kbps/upload_kbps are not verified byte counters. Never
     * integrate them into invented seven-day traffic totals. */
    memset(s->days,0,sizeof(s->days));
finish:
    if(!budget(begin)&&!s->retry_after_sec){s->api_ok=false;COPY(s->error,"Refresh time budget reached; some data cached");}
    if(any)s->fetched_epoch=now;
    s_maintenance++;
    close_session(&session);free(p);free(work);return s->api_ok?ESP_OK:ESP_FAIL;
}

esp_err_t unifi_api_poll(const unifi_config_t *cfg,unifi_snapshot_t *s,uint32_t client_offset,bool vpn_only,int device_index){
    return unifi_api_poll_with_progress(cfg,s,client_offset,vpn_only,device_index,NULL,NULL);
}

void unifi_api_demo(unifi_snapshot_t *s,uint32_t offset,bool vpn_only){
    int64_t now=time(NULL);if(now<1700000000)now=UNIFI_DEMO_EPOCH;
    unifi_demo_snapshot(s,now,offset,vpn_only);
}
