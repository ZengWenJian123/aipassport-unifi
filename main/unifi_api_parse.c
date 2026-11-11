#include "unifi_api_parse.h"
#include "unifi_site_names.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void unifi_copy_utf8(char *dst, size_t size, const char *src) {
    if (!size) return;
    size_t n=strlen(src); if(n>=size)n=size-1;
    while(n && ((unsigned char)src[n]&0xc0)==0x80)n--;
    memcpy(dst,src,n); dst[n]=0;
    for(size_t i=0;i<n;i++)if((unsigned char)dst[i]<32 || (unsigned char)dst[i]==127)dst[i]=' ';
}
#define COPY(d,s) unifi_copy_utf8((d),sizeof(d),(s))
/* Civil calendar conversion independent of libc timezone and ESP-IDF. */
static int64_t civil_days(int y, unsigned m, unsigned d) {
    y-=m<=2; int era=(y>=0?y:y-399)/400; unsigned yo=(unsigned)(y-era*400);
    unsigned doy=(153*(m+(m>2?(unsigned)-3:9))+2)/5+d-1;
    return (int64_t)era*146097+yo*365+yo/4-yo/100+doy-719468;
}
int64_t unifi_parse_time(const char *s) {
    int y,m,d,h,mi,se,n=0;
    if(sscanf(s,"%4d-%2d-%2dT%2d:%2d:%2d%n",&y,&m,&d,&h,&mi,&se,&n)!=6 || y<1970 || y>2100 || m<1 || m>12 || d<1 || d>31 || h<0 || h>23 || mi<0 || mi>59 || se<0 || se>59)return 0;
    static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int days=md[m-1]+(m==2 && ((y%4==0 && y%100!=0)||y%400==0));
    if(d>days)return 0;
    const char *p=s+n; if(*p=='.'){p++;if(!isdigit((unsigned char)*p))return 0;while(isdigit((unsigned char)*p))p++;}
    int offset=0;
    if(*p=='Z' && p[1]==0){} else if(*p=='+' || *p=='-'){
        int oh,om,used=0; if(sscanf(p+1,"%2d:%2d%n",&oh,&om,&used)!=2 || p[1+used] || oh<0 || oh>23 || om<0 || om>59)return 0;
        offset=(oh*60+om)*60*(*p=='+'?1:-1);
    }else return 0;
    return civil_days(y,(unsigned)m,(unsigned)d)*86400+h*3600+mi*60+se-offset;
}
static void canonical(const char *src,char *dst,size_t n){
    size_t j=0; while(*src && j+2<n){if(*src=='/' && isdigit((unsigned char)src[1])){dst[j++]='/';dst[j++]='#';src++;while(isdigit((unsigned char)*src))src++;}else dst[j++]=*src++;}dst[j]=0;
}
static bool suffix(const char *s,const char *t){size_t a=strlen(s),b=strlen(t);return a>=b && !strcmp(s+a-b,t);}
static int integer(const char *s){double x=strtod(s,NULL);return isfinite(x)&&x>=0&&x<10000000?(int)x:-1;}
static double number(const char *s){double x=strtod(s,NULL);return isfinite(x)&&x>=0?x:NAN;}
/* Parse only explicit controller WAN identifiers; never match an array order
 * to an unrelated integration UUID or infer online state from availability. */
static int wan_index(const char *key, size_t len) {
    if(len<3 || (strncmp(key,"WAN",3)&&strncmp(key,"wan",3)))return -1;
    if(len==3)return 0;
    unsigned n=0;
    for(size_t i=3;i<len;i++){if(key[i]<'0'||key[i]>'9'||n>100)return -1;n=n*10+(unsigned)(key[i]-'0');}
    return n? (int)n-1:-1;
}
static int wan_path(const char *path,const char *prefix,const char **field) {
    size_t n=strlen(prefix);if(strncmp(path,prefix,n))return -1;
    const char *key=path+n,*slash=strchr(key,'/');
    *field=slash?slash+1:"";
    return wan_index(key,slash?(size_t)(slash-key):strlen(key));
}
static bool host_match(unifi_parse_t *p){return !p->wanted_host[0] || !strcmp(p->host,p->wanted_host);}
static bool site_match(unifi_parse_t *p){return host_match(p)&&(!p->wanted_site[0] || !strcmp(p->site,p->wanted_site));}
static void unknown_device(unifi_device_t *d){memset(d,0,sizeof(*d));d->cpu_pct=d->memory_pct=d->rx_bps=d->tx_bps=NAN;}
static void metric_reset(unifi_parse_t *p){
    p->metric_fields=0;p->metric_time=0;p->avg=p->max=p->loss=p->down=p->up=NAN;
}
static void group_metrics_reset(unifi_parse_t *p){
    p->host[0]=p->site[0]=0;p->latest=0;
    memset(p->hist_down,0,sizeof(p->hist_down));memset(p->hist_up,0,sizeof(p->hist_up));memset(p->hist_mask,0,sizeof(p->hist_mask));
    memset(p->hist_latency,0,sizeof(p->hist_latency));memset(p->hist_loss,0,sizeof(p->hist_loss));
    memset(p->hist_latency_mask,0,sizeof(p->hist_latency_mask));memset(p->hist_loss_mask,0,sizeof(p->hist_loss_mask));
    memset(p->days,0,sizeof(p->days));memset(p->day_mask,0,sizeof(p->day_mask));
    int64_t today=((p->now+28800)/86400)*86400-28800;
    for(int i=0;i<7;i++)p->days[i].day_epoch=(int32_t)(today-(6-i)*86400);
}
static void metric_period(unifi_parse_t *p){
    if(p->metric_time<=0 || p->metric_time>p->now+60)return;
    if(p->kind==UNIFI_PARSE_METRICS_5M){
        if(p->metric_time<p->now-86400)return;
        if((p->metric_fields&31)==31 && p->metric_time>=p->latest){
            p->latest=p->metric_time;p->latest_avg=p->avg;p->latest_max=p->max;p->latest_loss=p->loss;p->latest_down=p->down;p->latest_up=p->up;
        }
        int64_t start=(p->now/1800-47)*1800;int bin=(int)((p->metric_time-start)/1800);
        if(p->metric_time>=start && bin>=0 && bin<UNIFI_HISTORY_POINTS){
            unsigned bit=1u<<((p->metric_time%1800)/300);
            if((p->metric_fields&24)==24 && !(p->hist_mask[bin]&bit)){p->hist_mask[bin]|=bit;p->hist_down[bin]+=p->down;p->hist_up[bin]+=p->up;}
            /* Each series uses only its own available five-minute samples.
             * Partial/duplicate records cannot create zeroes or alter weights. */
            if((p->metric_fields&1) && !(p->hist_latency_mask[bin]&bit)){p->hist_latency_mask[bin]|=bit;p->hist_latency[bin]+=p->avg;}
            if((p->metric_fields&4) && !(p->hist_loss_mask[bin]&bit)){p->hist_loss_mask[bin]|=bit;p->hist_loss[bin]+=p->loss;}
        }
    }
    /* One-hour ISP speeds are not byte counters. Do not derive daily usage. */
}
static float history_average(double sum,unsigned mask){
    unsigned count=0;while(mask){count+=mask&1;mask>>=1;}
    return count?(float)(sum/count):NAN;
}
static void commit_metrics(unifi_parse_t *p){
    if(!site_match(p))return;
    p->matched=true;
    if(p->kind==UNIFI_PARSE_METRICS_1H){memcpy(p->out->days,p->days,sizeof(p->days));return;}
    if(p->latest){p->out->metrics_valid=true;p->out->metric_epoch=p->latest;p->out->avg_latency_ms=p->latest_avg;p->out->max_latency_ms=p->latest_max;p->out->loss_pct=p->latest_loss;p->out->down_bps=p->latest_down;p->out->up_bps=p->latest_up;}
    p->out->history_count=UNIFI_HISTORY_POINTS;
    for(int i=0;i<UNIFI_HISTORY_POINTS;i++){
        p->out->history_down[i]=history_average(p->hist_down[i],p->hist_mask[i]);
        p->out->history_up[i]=history_average(p->hist_up[i],p->hist_mask[i]);
        p->out->history_latency[i]=history_average(p->hist_latency[i],p->hist_latency_mask[i]);
        p->out->history_loss[i]=history_average(p->hist_loss[i],p->hist_loss_mask[i]);
    }
}
static void device_field(unifi_device_t *d,const char *key,const char *v,ujson_event_t ev){
    if(!strcmp(key,"id"))COPY(d->id,v);
    else if(!strcmp(key,"name"))COPY(d->name,v);
    else if(!strcmp(key,"status") || !strcmp(key,"state"))COPY(d->state,v);
    else if(!strcmp(key,"startupTime") && ev==UJSON_STRING)d->startup_epoch=unifi_parse_time(v);
}
static void client_commit(unifi_parse_t *p){
    unifi_clients_t *c=&p->out->clients;unifi_client_t *r=&p->row_client;
    p->rows++;
    if(!p->client_id[0]){p->truncated=true;return;}
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const unsigned char *b=(const unsigned char *)p->client_id;*b;b++)hash=(hash^*b)*UINT64_C(1099511628211);
    for(unsigned i=0;i<p->client_hash_count;i++)if(p->client_hashes[i]==hash){p->truncated=true;return;}
    if(p->client_hash_count>=300){p->truncated=true;return;}
    p->client_hashes[p->client_hash_count++]=hash;
    c->total++;if(r->guest)c->guest++;
    if(!strcmp(r->type,"WIRELESS"))c->wireless++;
    else if(!strcmp(r->type,"WIRED"))c->wired++;
    else if(!strcmp(r->type,"VPN"))c->vpn++;
    else if(!strcmp(r->type,"TELEPORT"))c->teleport++;
    else p->truncated=true;
    bool vpn=!strcmp(r->type,"VPN");
    if(vpn && r->connected_epoch>0 && r->connected_epoch<=p->now && (!c->latest_vpn_valid || r->connected_epoch>c->latest_vpn.connected_epoch)){c->latest_vpn=*r;c->latest_vpn_valid=true;}
    if(!p->vpn_only || vpn || !strcmp(r->type,"TELEPORT")){
        if(p->selected_seen>=p->selected_offset && c->returned<UNIFI_CLIENT_PAGE_SIZE)c->items[c->returned++]=*r;
        p->selected_seen++;
    }
}
static bool on_event(void *user,ujson_event_t ev,const char *path,const char *v){
    unifi_parse_t *p=user;char q[512];canonical(path,q,sizeof(q));
    bool scalar=ev>=UJSON_STRING, numeric=ev==UJSON_NUMBER;
    const char *key=strrchr(q,'/');key=key?key+1:q;
    if(ev==UJSON_ARRAY && !strcmp(q,"/data"))p->schema_ok=true;
    if(scalar && !strcmp(q,"/nextToken") && ev==UJSON_STRING)COPY(p->next_token,v);
    if(numeric && !strcmp(q,"/totalCount"))p->total_count=(unsigned)integer(v);
    if(p->kind==UNIFI_PARSE_HOSTS){
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#")){p->host[0]=0;p->row_site.name[0]=0;p->reference[0]=0;}
        if(ev==UJSON_STRING){
            if(!strcmp(q,"/data/#/id"))COPY(p->host,v);
            else if(!strcmp(q,"/data/#/reportedState/name"))COPY(p->row_site.name,v);
            else if(!strcmp(q,"/data/#/reportedState/hostname"))COPY(p->reference,v);
        }
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")){
            p->rows++;
            const char *name=p->row_site.name[0]?p->row_site.name:p->reference;
            if(name[0])for(unsigned i=0;i<p->out->site_count;i++){
                unifi_site_t *site=&p->out->sites[i];
                if(!strcmp(site->host_id,p->host)&&unifi_site_generic_name(site->name)){COPY(site->name,name);p->matched=true;}
            }
        }
    }else if(p->kind==UNIFI_PARSE_SITES){
        if(ev==UJSON_OBJECT && !strcmp(q,"/data/#")){memset(&p->row_site,0,sizeof(p->row_site));p->row_site.clients=p->row_site.devices_online=p->row_site.devices_total=p->row_site.internet_issues=-1;COPY(p->row_site.status,"unknown");p->wifi=p->wired=p->total_devices=p->offline_devices=-1;p->reference[0]=0;}
        if(ev==UJSON_ARRAY&&!strcmp(q,"/data/#/statistics/internetIssues")){p->row_site.internet_issues=0;p->row_site.internet_issues_valid=true;}
        if((ev==UJSON_OBJECT||scalar)&&!strcmp(q,"/data/#/statistics/internetIssues/#"))p->row_site.internet_issues++;
        if(scalar){
            if(!strcmp(q,"/data/#/siteId"))COPY(p->row_site.site_id,v);
            else if(!strcmp(q,"/data/#/hostId"))COPY(p->row_site.host_id,v);
            else if(!strcmp(q,"/data/#/meta/desc"))COPY(p->row_site.name,v);
            else if(!strcmp(q,"/data/#/meta/name")){COPY(p->reference,v);if(!p->row_site.name[0])COPY(p->row_site.name,v);}
            else if(!strcmp(q,"/data/#/statistics/ispInfo/name")&&ev==UJSON_STRING)COPY(p->row_site.isp_name,v);
            else if(!strcmp(q,"/data/#/statistics/ispInfo/organization")&&ev==UJSON_STRING)COPY(p->row_site.isp_organization,v);
            else if(!strcmp(q,"/data/#/statistics/counts/wifiClient")&&numeric)p->wifi=integer(v);
            else if(!strcmp(q,"/data/#/statistics/counts/wiredClient")&&numeric)p->wired=integer(v);
            else if(!strcmp(q,"/data/#/statistics/counts/totalDevice")&&numeric)p->total_devices=integer(v);
            else if(!strcmp(q,"/data/#/statistics/counts/offlineDevice")&&numeric)p->offline_devices=integer(v);
            else if(!strcmp(q,"/data/#/statistics/percentages/wanUptime")&&numeric){p->row_site.wan_uptime_pct=number(v);p->row_site.uptime_valid=isfinite(p->row_site.wan_uptime_pct);}
        }
        if(ev==UJSON_END_OBJECT && !strcmp(q,"/data/#")){
            p->rows++;unifi_site_t *r=&p->row_site;
            if(p->wifi>=0&&p->wired>=0)r->clients=p->wifi+p->wired;
            /* Site Manager exposes counts, not a canonical site/WAN health flag. */
            if(p->total_devices>=0&&p->offline_devices>=0&&p->offline_devices<=p->total_devices){r->devices_total=p->total_devices;r->devices_online=p->total_devices-p->offline_devices;r->counts_valid=true;COPY(r->status,r->devices_online>0?"devices_online":"devices_offline");}
            if(p->out->site_count<UNIFI_SITE_MAX){unsigned i=p->out->site_count++;p->out->sites[i]=*r;if((!p->wanted_host[0]&&!p->wanted_site[0]&&i==0)||(!strcmp(r->host_id,p->wanted_host)&&(!p->wanted_site[0]||!strcmp(r->site_id,p->wanted_site)))){p->out->selected_site=i;p->matched=true;COPY(p->selected_reference,p->reference);}}
            else p->truncated=true;
        }
    }else if(p->kind==UNIFI_PARSE_NETWORK_SITES){
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#"))p->site[0]=p->reference[0]=p->row_site.name[0]=0;
        if(scalar && !strcmp(q,"/data/#/id"))COPY(p->site,v);
        if(scalar && !strcmp(q,"/data/#/internalReference"))COPY(p->reference,v);
        if(scalar && !strcmp(q,"/data/#/name"))COPY(p->row_site.name,v);
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")){p->rows++;if(p->rows==1)COPY(p->network_candidate,p->site);if(!strcmp(p->site,p->wanted_site)||(p->wanted_reference[0]&&!strcmp(p->reference,p->wanted_reference))){COPY(p->out->sites[p->out->selected_site].network_site_id,p->site);if(!strcmp(p->out->sites[p->out->selected_site].name,"Local console")&&p->row_site.name[0])COPY(p->out->sites[p->out->selected_site].name,p->row_site.name);COPY(p->selected_reference,p->reference);p->matched=true;}}
    }else if(p->kind==UNIFI_PARSE_DEVICES || p->kind==UNIFI_PARSE_NETWORK_DEVICES){
        bool network=p->kind==UNIFI_PARSE_NETWORK_DEVICES;const char *row=network?"/data/#":"/data/#/devices/#";
        if(!network && ev==UJSON_OBJECT&&!strcmp(q,"/data/#")){p->host[0]=0;p->group_count=p->group_total=p->group_offline=0;}
        if(!network && scalar&&!strcmp(q,"/data/#/hostId"))COPY(p->host,v);
        if(ev==UJSON_OBJECT&&!strcmp(q,row))unknown_device(&p->row_device);
        size_t rlen=strlen(row);
        if(scalar&&!strncmp(q,row,rlen)&&q[rlen]=='/'&&!strchr(q+rlen+1,'/'))device_field(&p->row_device,key,v,ev);
        if(ev==UJSON_END_OBJECT&&!strcmp(q,row)){
            unifi_device_t *d=&p->row_device;p->rows++;
            if(d->startup_epoch>0&&d->startup_epoch<=p->now){d->uptime_valid=true;d->uptime_sec=p->now-d->startup_epoch;}
            if(network){
                if(p->out->device_count<UNIFI_DEVICE_MAX)p->out->devices[p->out->device_count++]=*d;else p->truncated=true;
                p->out->device_total++;if(strcasecmp(d->state,"online"))p->out->device_offline++;
            }else{
                p->group_total++;if(strcasecmp(d->state,"online"))p->group_offline++;
                if(p->group_count<UNIFI_DEVICE_MAX)p->group_devices[p->group_count++]=*d;else p->truncated=true;
            }
        }
        if(!network && ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")&&host_match(p)){
            p->matched=true;p->out->device_count=p->group_count;p->out->device_total=p->group_total;p->out->device_offline=p->group_offline;memcpy(p->out->devices,p->group_devices,sizeof(p->group_devices));
        }
    }else if(p->kind==UNIFI_PARSE_DEVICE_STATS){
        if(ev==UJSON_OBJECT&&!q[0])p->schema_ok=true;
        unifi_device_t *d=&p->out->devices[p->target_device];double x=numeric?number(v):NAN;
        if(!strcmp(q,"/cpuUtilizationPct")){d->cpu_pct=x;d->cpu_valid=isfinite(x);}
        else if(!strcmp(q,"/memoryUtilizationPct")){d->memory_pct=x;d->memory_valid=isfinite(x);}
        else if(!strcmp(q,"/uptimeSec")&&numeric){d->uptime_valid=isfinite(x)&&x<=p->now;if(d->uptime_valid){d->uptime_sec=(int64_t)x;d->startup_epoch=p->now-d->uptime_sec;}}
        /* Raw API Bps values: no assumed conversion or WAN direction attribution. */
        else if(!strcmp(q,"/uplink/rxRateBps"))d->rx_bps=x;
        else if(!strcmp(q,"/uplink/txRateBps"))d->tx_bps=x;
        d->rates_valid=isfinite(d->rx_bps)&&isfinite(d->tx_bps);
    }else if(p->kind==UNIFI_PARSE_CLIENTS){
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#")){memset(&p->row_client,0,sizeof(p->row_client));p->client_id[0]=0;}
        if(scalar){
            if(!strcmp(q,"/data/#/id"))COPY(p->client_id,v);
            else if(!strcmp(q,"/data/#/name"))COPY(p->row_client.name,v);
            else if(!strcmp(q,"/data/#/type"))COPY(p->row_client.type,v);
            else if(!strcmp(q,"/data/#/ipAddress")&&ev==UJSON_STRING)COPY(p->row_client.ip,v);
            else if(!strcmp(q,"/data/#/connectedAt")&&ev==UJSON_STRING)p->row_client.connected_epoch=unifi_parse_time(v);
            else if(!strcmp(q,"/data/#/access/type"))p->row_client.guest=!strcmp(v,"GUEST");
        }
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#"))client_commit(p);
    }else if(p->kind==UNIFI_PARSE_METRICS_5M || p->kind==UNIFI_PARSE_METRICS_1H){
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#"))group_metrics_reset(p);
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#/periods/#"))metric_reset(p);
        if(scalar){
            if(!strcmp(q,"/data/#/hostId"))COPY(p->host,v);
            else if(!strcmp(q,"/data/#/siteId"))COPY(p->site,v);
            else if(!strcmp(q,"/data/#/periods/#/metricTime")&&ev==UJSON_STRING)p->metric_time=unifi_parse_time(v);
            else if(!strncmp(q,"/data/#/periods/#/data/wan/",strlen("/data/#/periods/#/data/wan/"))&&numeric){
                double x=number(v);if(isfinite(x)){
                    if(!strcmp(key,"avgLatency")){p->avg=x;p->metric_fields|=1;}
                    else if(!strcmp(key,"maxLatency")){p->max=x;p->metric_fields|=2;}
                    else if(!strcmp(key,"packetLoss")){p->loss=x;p->metric_fields|=4;}
                    else if(!strcmp(key,"download_kbps")&&x<=1e12){p->down=x*1000;p->metric_fields|=8;}
                    else if(!strcmp(key,"upload_kbps")&&x<=1e12){p->up=x*1000;p->metric_fields|=16;}
                }
            }
        }
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#/periods/#"))metric_period(p);
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#"))commit_metrics(p);
    }else if(p->kind==UNIFI_PARSE_WAN){
        if(ev==UJSON_STRING&&!strcmp(q,"/meta/rc"))p->legacy_ok=!strcmp(v,"ok");
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#")){p->reference[0]=0;p->gateway_mac[0]=0;p->down=p->up=NAN;memset(p->row_wans,0,sizeof p->row_wans);}
        if(ev==UJSON_STRING&&!strcmp(q,"/data/#/subsystem"))COPY(p->reference,v);
        if(ev==UJSON_STRING&&!strcmp(q,"/data/#/gw_mac"))COPY(p->gateway_mac,v);
        const char *field;int wi=wan_path(q,"/data/#/uptime_stats/",&field);
        if(wi>=0&&wi<UNIFI_WAN_MAX&&numeric){
            unifi_wan_t *w=&p->row_wans[wi];double x=number(v);
            if(!strcmp(field,"availability")&&isfinite(x)&&x>=0&&x<=100){w->availability_pct=x;w->history_epoch=p->now;}
            if(!strcmp(field,"time_period")&&isfinite(x)&&x>0&&x<=UINT32_MAX)w->period_sec=(uint32_t)x;
        }
        if(numeric&&!strcmp(q,"/data/#/rx_bytes-r"))p->down=number(v);
        if(numeric&&!strcmp(q,"/data/#/tx_bytes-r"))p->up=number(v);
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")&&!strcmp(p->reference,"wan")&&isfinite(p->down)&&isfinite(p->up)&&p->down<1e12&&p->up<1e12){
            p->out->wan_down_bps=p->down*8;p->out->wan_up_bps=p->up*8;COPY(p->out->wan_gateway_mac,p->gateway_mac);p->matched=true;
            for(unsigned i=0;i<UNIFI_WAN_MAX;i++){
                p->out->wans[i].availability_pct=p->row_wans[i].availability_pct;
                p->out->wans[i].history_epoch=p->row_wans[i].history_epoch;p->out->wans[i].period_sec=p->row_wans[i].period_sec;
            }
        }
    }else if(p->kind==UNIFI_PARSE_WAN_LINKS){
        if(ev==UJSON_STRING&&!strcmp(q,"/meta/rc"))p->legacy_ok=!strcmp(v,"ok");
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#")){
            memset(p->row_wans,0,sizeof p->row_wans);p->gateway_type[0]=p->gateway_mac[0]=0;p->wan_overflow=false;
        }
        if(ev==UJSON_STRING&&!strcmp(q,"/data/#/type"))COPY(p->gateway_type,v);
        if(ev==UJSON_STRING&&!strcmp(q,"/data/#/mac"))COPY(p->gateway_mac,v);
        const char *field;int wi=wan_path(q,"/data/#/",&field);
        bool status=false,alive=false;
        if(wi<0){wi=wan_path(q,"/data/#/last_wan_status/",&field);status=true;}
        if(wi<0){wi=wan_path(q,"/data/#/last_wan_interfaces/",&field);status=false;alive=true;}
        if(wi>=UNIFI_WAN_MAX)p->wan_overflow=true;
        if(wi>=0&&wi<UNIFI_WAN_MAX){
            unifi_wan_t *w=&p->row_wans[wi];
            if(ev==UJSON_OBJECT||ev==UJSON_STRING||ev==UJSON_BOOL)w->present=true;
            if(status&&!*field&&ev==UJSON_STRING)COPY(w->state,v);
            /* alive describes connectivity but does not override an explicit
             * last_wan_status. Missing fields remain unknown, never zero/UP. */
            if(alive&&!strcmp(field,"alive")&&ev==UJSON_BOOL&&!w->state[0])COPY(w->state,!strcmp(v,"true")?"online":"offline");
            if(!status&&!alive&&ev==UJSON_BOOL){
                bool yes=!strcmp(v,"true");
                if(!strcmp(field,"up")){w->link_known=true;w->link_up=yes;}
                if(!strcmp(field,"enable")){w->enabled_known=true;w->enabled=yes;}
                if(!strcmp(field,"is_uplink"))w->active=yes;
            }
        }
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")&&
           (!strcmp(p->gateway_type,"udm")||!strcmp(p->gateway_type,"ugw")||!strcmp(p->gateway_type,"uxg"))&&
           (!p->wanted_host[0]||!strcmp(p->gateway_mac,p->wanted_host))){
            p->rows++;
            for(unsigned i=0;i<UNIFI_WAN_MAX;i++){
                unifi_wan_t old=p->out->wans[i];p->out->wans[i]=p->row_wans[i];
                p->out->wans[i].availability_pct=old.availability_pct;
                p->out->wans[i].history_epoch=old.history_epoch;p->out->wans[i].period_sec=old.period_sec;
            }
            p->out->wans_complete=!p->wan_overflow;p->matched=true;
        }
    }else if(p->kind==UNIFI_PARSE_VPN){
        if(ev==UJSON_STRING&&!strcmp(q,"/meta/rc"))p->legacy_ok=!strcmp(v,"ok");
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#")){
            memset(&p->row_client,0,sizeof(p->row_client));p->client_id[0]=0;p->row_up=false;p->vpn_uptime=-1;p->vpn_last_seen=0;
        }
        if(ev==UJSON_STRING){
            if(!strcmp(q,"/data/#/id"))COPY(p->client_id,v);
            else if(!strcmp(q,"/data/#/user"))COPY(p->row_client.name,v);
            else if(!strcmp(q,"/data/#/tunnel_ip"))COPY(p->row_client.ip,v);
            else if(!strcmp(q,"/data/#/vpn_type"))COPY(p->row_client.type,!strcmp(v,"teleport")?"TELEPORT":"VPN");
        }
        if(ev==UJSON_BOOL&&!strcmp(q,"/data/#/up"))p->row_up=!strcmp(v,"true");
        if(numeric&&!strcmp(q,"/data/#/uptime")){double x=number(v);if(isfinite(x)&&x<=p->now)p->vpn_uptime=(int64_t)x;}
        if(numeric&&!strcmp(q,"/data/#/last_seen")){double x=number(v);if(isfinite(x)&&x<=p->now+60)p->vpn_last_seen=(int64_t)x;}
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")&&p->row_up){
            if(p->vpn_uptime>=0){int64_t end=p->vpn_last_seen?p->vpn_last_seen:p->now;if(end>=p->vpn_uptime)p->row_client.connected_epoch=end-p->vpn_uptime;}
            if(!p->row_client.name[0])COPY(p->row_client.name,"VPN client");
            if(!p->row_client.type[0]){p->truncated=true;return true;}
            client_commit(p);
        }
    }else if(p->kind==UNIFI_PARSE_SDWAN_CONFIG){
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data/#"))p->site[0]=p->reference[0]=0;
        if(scalar&&!strcmp(q,"/data/#/id"))COPY(p->site,v);
        if(scalar&&!strcmp(q,"/data/#/type"))COPY(p->reference,v);
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/#")){p->rows++;if(p->rows==1){COPY(p->config_id,p->site);COPY(p->config_type,p->reference);}}
    }else if(p->kind==UNIFI_PARSE_SDWAN_STATUS){
        if(ev==UJSON_OBJECT&&!strcmp(q,"/data"))p->schema_ok=true;
        if(!strncmp(q,"/data/status/",13)){
            const char *conn="/data/status/connections/#", *ep="/data/status/connections/#/sites/#";
            if(ev==UJSON_OBJECT&&!strcmp(q,conn)){p->mesh_member=false;p->mesh_connected=p->mesh_known=true;p->mesh_endpoints=0;p->row_site.host_id[0]=p->row_site.site_id[0]=0;}
            if(ev==UJSON_OBJECT&&!strcmp(q,ep)){p->host[0]=p->site[0]=0;p->mesh_flags=0;p->mesh_endpoint_connected=false;p->mesh_endpoint_offline=true;}
            if(ev==UJSON_STRING&&!strcmp(q,"/data/status/connections/#/sites/#/hostId"))COPY(p->host,v);
            if(ev==UJSON_STRING&&!strcmp(q,"/data/status/connections/#/sites/#/siteId"))COPY(p->site,v);
            if(ev==UJSON_BOOL&&!strcmp(q,"/data/status/connections/#/sites/#/status/connected")){p->mesh_flags|=1;p->mesh_endpoint_connected=!strcmp(v,"true");}
            if(ev==UJSON_BOOL&&!strcmp(q,"/data/status/connections/#/sites/#/status/offline")){p->mesh_flags|=2;p->mesh_endpoint_offline=!strcmp(v,"true");}
            if(ev==UJSON_END_OBJECT&&!strcmp(q,ep)){
                p->mesh_endpoints++;p->mesh_known&=p->mesh_flags==3;
                p->mesh_connected&=p->mesh_endpoint_connected&&!p->mesh_endpoint_offline;
                if(site_match(p))p->mesh_member=true;
                else {COPY(p->row_site.host_id,p->host);COPY(p->row_site.site_id,p->site);}
            }
            if(ev==UJSON_END_OBJECT&&!strcmp(q,conn)&&p->mesh_member){
                p->sd_has_host=true;p->sd_status_seen=true;p->out->sdwan.total++;
                if(!p->mesh_known||p->mesh_endpoints<2)p->truncated=true;
                if(p->mesh_known&&p->mesh_endpoints>=2&&p->mesh_connected)p->out->sdwan.connected++;
                COPY(p->out->sdwan.remote_host,p->row_site.host_id);COPY(p->out->sdwan.remote_site,p->row_site.site_id);
            }
            if(ev==UJSON_OBJECT&&!strcmp(q,"/data/status/sites/#")){p->host[0]=p->site[0]=0;p->primary_issues=p->backup_issues=0;}
            if(ev==UJSON_STRING&&!strcmp(q,"/data/status/sites/#/hostId"))COPY(p->host,v);
            if(ev==UJSON_STRING&&!strcmp(q,"/data/status/sites/#/siteId"))COPY(p->site,v);
            if((scalar||ev==UJSON_OBJECT)&&!strcmp(q,"/data/status/sites/#/errors/#"))p->primary_issues++;
            if((scalar||ev==UJSON_OBJECT)&&!strcmp(q,"/data/status/sites/#/warnings/#"))p->backup_issues++;
            if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/status/sites/#")){
                p->out->sdwan.peers++;
                if(site_match(p)){p->out->sdwan.errors+=p->primary_issues;p->out->sdwan.warnings+=p->backup_issues;}
            }
            return true;
        }
        bool endpoint=!strcmp(q,"/data/hubs/#")||!strcmp(q,"/data/spokes/#");
        if(ev==UJSON_OBJECT&&endpoint){p->host[0]=0;p->primary_configured=p->backup_configured=p->primary_issues_known=p->backup_issues_known=false;p->primary_issues=p->backup_issues=0;p->primary_latency=p->backup_latency=NAN;}
        if(scalar&&(!strcmp(q,"/data/hubs/#/hostId")||!strcmp(q,"/data/spokes/#/hostId")))COPY(p->host,v);
        if(scalar&&suffix(q,"/hostId")&&!strcmp(v,p->wanted_host))p->sd_has_host=true;
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/hubs/#"))p->out->sdwan.hubs++;
        if(ev==UJSON_END_OBJECT&&!strcmp(q,"/data/spokes/#"))p->out->sdwan.spokes++;
        if(ev==UJSON_END_OBJECT&&suffix(q,"/errors/#"))p->out->sdwan.errors++;
        if(ev==UJSON_END_OBJECT&&suffix(q,"/warnings/#"))p->out->sdwan.warnings++;
        if(scalar&&suffix(q,"/tunnels/#/status")){p->out->sdwan.total++;if(!strcmp(v,"connected"))p->out->sdwan.connected++;p->sd_status_seen=true;}
        if(scalar&&suffix(q,"/primaryWanStatus/wanId")&&v[0])p->primary_configured=true;
        if(scalar&&suffix(q,"/secondaryWanStatus/wanId")&&v[0])p->backup_configured=true;
        if(numeric&&suffix(q,"/primaryWanStatus/latency"))p->primary_latency=number(v);
        if(numeric&&suffix(q,"/secondaryWanStatus/latency"))p->backup_latency=number(v);
        if(ev==UJSON_OBJECT&&suffix(q,"/primaryWanStatus/internetIssues/#"))p->primary_issues++;
        if(ev==UJSON_OBJECT&&suffix(q,"/secondaryWanStatus/internetIssues/#"))p->backup_issues++;
        if(ev==UJSON_END_ARRAY&&suffix(q,"/primaryWanStatus/internetIssues"))p->primary_issues_known=true;
        if(ev==UJSON_END_ARRAY&&suffix(q,"/secondaryWanStatus/internetIssues"))p->backup_issues_known=true;
        if(ev==UJSON_END_OBJECT&&endpoint&&host_match(p)){
            unifi_sdwan_t *sw=&p->out->sdwan;
            /* Report absence of API issues, never infer link UP just from wanId. */
            COPY(sw->primary,p->primary_configured?(p->primary_issues_known?(p->primary_issues?"issues":"no_issues"):"configured"):"unknown");
            COPY(sw->backup,p->backup_configured?(p->backup_issues_known?(p->backup_issues?"issues":"no_issues"):"configured"):"unknown");
            sw->primary_latency_valid=isfinite(p->primary_latency);sw->backup_latency_valid=isfinite(p->backup_latency);
            sw->primary_latency_ms=p->primary_latency;sw->backup_latency_ms=p->backup_latency;
        }
    }
    return true;
}
void unifi_parse_init(unifi_parse_t *p,unifi_parse_kind_t kind,unifi_snapshot_t *out,const char *host,const char *site,const char *reference,int64_t now){
    memset(p,0,sizeof(*p));p->kind=kind;p->out=out;p->now=now;COPY(p->wanted_host,host?host:"");COPY(p->wanted_site,site?site:"");COPY(p->wanted_reference,reference?reference:"");
    ujson_init(&p->json,on_event,p);
    if(kind==UNIFI_PARSE_WAN)for(unsigned i=0;i<UNIFI_WAN_MAX;i++){
        out->wans[i].availability_pct=NAN;out->wans[i].history_epoch=0;out->wans[i].period_sec=0;
    }
    if(kind==UNIFI_PARSE_SITES){out->site_count=0;out->selected_site=0;}
    if(kind==UNIFI_PARSE_METRICS_5M){out->metrics_valid=false;out->metric_epoch=0;}
    if(kind==UNIFI_PARSE_METRICS_1H)memset(out->days,0,sizeof(out->days));
    if(kind==UNIFI_PARSE_DEVICE_STATS)for(unsigned i=0;i<out->device_count;i++){
        out->devices[i].rates_valid=out->devices[i].cpu_valid=out->devices[i].memory_valid=out->devices[i].uptime_valid=false;
        out->devices[i].rx_bps=out->devices[i].tx_bps=out->devices[i].cpu_pct=out->devices[i].memory_pct=NAN;
    }
    if(kind==UNIFI_PARSE_DEVICES || kind==UNIFI_PARSE_NETWORK_DEVICES){out->device_count=0;out->device_total=out->device_offline=0;}
    if(kind==UNIFI_PARSE_SDWAN_STATUS){memset(&out->sdwan,0,sizeof(out->sdwan));COPY(out->sdwan.primary,"unknown");COPY(out->sdwan.backup,"unknown");}
}
bool unifi_parse_feed(unifi_parse_t *p,const char *s,size_t n){return ujson_feed(&p->json,s,n);}
void unifi_parse_next_client_page(unifi_parse_t *p){p->rows=0;p->total_count=0;p->schema_ok=false;ujson_init(&p->json,on_event,p);}
bool unifi_parse_finish(unifi_parse_t *p){
    if(!ujson_finish(&p->json)||!p->schema_ok)return false;
    if((p->kind==UNIFI_PARSE_WAN||p->kind==UNIFI_PARSE_VPN||p->kind==UNIFI_PARSE_WAN_LINKS)&&!p->legacy_ok)return false;
    if(p->kind==UNIFI_PARSE_WAN&&!p->matched)return false;
    if(p->kind==UNIFI_PARSE_WAN_LINKS){
        unsigned count=0;for(unsigned i=0;i<UNIFI_WAN_MAX;i++)if(p->out->wans[i].present)count++;
        if(!p->matched||p->rows!=1||!count)return false;
    }
    if(p->kind==UNIFI_PARSE_NETWORK_SITES&&!p->matched&&p->rows==1&&p->total_count<=1){COPY(p->out->sites[p->out->selected_site].network_site_id,p->network_candidate);if(!strcmp(p->out->sites[p->out->selected_site].name,"Local console")&&p->row_site.name[0])COPY(p->out->sites[p->out->selected_site].name,p->row_site.name);COPY(p->selected_reference,p->reference);p->matched=true;}
    if(p->kind==UNIFI_PARSE_DEVICES || p->kind==UNIFI_PARSE_NETWORK_DEVICES){
        if(p->kind==UNIFI_PARSE_NETWORK_DEVICES && p->total_count>p->rows)p->truncated=true;
        p->out->devices_complete=!p->truncated&&!p->next_token[0];
    }
    if(p->kind==UNIFI_PARSE_SDWAN_STATUS){p->out->sdwan.available=p->sd_has_host&&p->sd_status_seen&&!p->truncated;if(!p->out->sdwan.available)COPY(p->out->sdwan.error,"Mesh or selected site not covered by API");}
    return true;
}
