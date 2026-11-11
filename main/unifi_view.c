#include "unifi_view.h"
#include "unifi_site_names.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Native vector display lists mirror the reviewed 240 x 320 design. */
#define BG 0x111315
#define CARD 0x1D2024
#define EDGE 0x31353B
#define FG 0xEDF0F4
#define MUTED 0xA4ACB8
#define BLUE 0x429AFF
#define CYAN 0x53C8EE
#define PURPLE 0xBB8FE8
#define GREEN 0x50CE83
#define AMBER 0xEFD157
#define RED 0xFF8585

static void txt(const unifi_canvas_t *c,int x,int y,int w,int f,uint32_t col,const char *s)
{ c->text(c->ctx,x,y,w,f,col,s); }
static void box(const unifi_canvas_t *c,int x,int y,int w,int h,int r,uint32_t col)
{ if(w>0 && h>0)c->rect(c->ctx,x,y,w,h,r,col); }
static void ln(const unifi_canvas_t *c,int x,int y,int xx,int yy,int w,uint32_t col)
{ c->line(c->ctx,x,y,xx,yy,w,col); }
static void arc(const unifi_canvas_t *c,int x,int y,int r,int a,int b,int w,uint32_t col)
{ c->arc(c->ctx,x,y,r,a,b,w,col); }
static void panel(const unifi_canvas_t *c,int x,int y,int w,int h,uint32_t edge,uint32_t fill)
{ box(c,x,y,w,h,7,edge);box(c,x+1,y+1,w-2,h-2,6,fill); }
static void number(const unifi_canvas_t *c,int x,int y,int w,int value,uint32_t col)
{ char b[24];if(value<0)snprintf(b,sizeof b,"--");else snprintf(b,sizeof b,"%d",value);txt(c,x,y,w,24,col,b); }
static const unifi_site_t *site(const unifi_snapshot_t *s)
{
    static const unifi_site_t unknown = {.clients=-1,.devices_online=-1,.devices_total=-1};
    return s->selected_site<s->site_count?&s->sites[s->selected_site]:&unknown;
}
static bool online(const char *state)
{ return !strcmp(state,"online")||!strcmp(state,"ONLINE")||!strcmp(state,"connected"); }
static void metric(const unifi_canvas_t *c,int x,int y,int w,const char *label,const char *value,uint32_t col)
{ txt(c,x,y,w,12,MUTED,label);txt(c,x,y+20,w,18,col,value); }
static void pair(const unifi_canvas_t *c,int y,const char *label,const char *value)
{ txt(c,14,y,90,12,MUTED,label);txt(c,110,y,116,12,FG,value); }

/* Icons share the 18-unit coordinate grid of the prototype SVGs. */
enum { I_WIFI=U_PAGE_COUNT,I_BATTERY,I_CHECK,I_KEY,I_ARROW_DOWN,I_ARROW_UP,I_WARNING };
static void il(const unifi_canvas_t *c,int x,int y,int size,int a,int b,int d,int e,uint32_t color)
{ ln(c,x+a*size/18,y+b*size/18,x+d*size/18,y+e*size/18,1,color); }
static void ib(const unifi_canvas_t *c,int x,int y,int size,int a,int b,int w,int h,uint32_t color)
{
    int xx=x+a*size/18,yy=y+b*size/18,ww=w*size/18,hh=h*size/18;
    box(c,xx,yy,ww,hh,1,color);box(c,xx+1,yy+1,ww-2,hh-2,0,BG);
}
static void icon(const unifi_canvas_t *c,int which,int x,int y,int size,uint32_t col)
{
#define L(a,b,d,e) il(c,x,y,size,a,b,d,e,col)
#define B(a,b,w,h) ib(c,x,y,size,a,b,w,h,col)
    switch(which) {
    case U_OVERVIEW: B(1,1,6,8);B(11,1,6,5);B(1,12,6,5);B(11,9,6,8);break;
    case U_INTERNET: L(0,10,4,10);L(4,10,7,2);L(7,2,11,16);L(11,16,14,8);L(14,8,18,8);break;
    case U_CLIENTS: B(0,2,12,9);L(6,11,6,14);L(3,14,8,14);B(12,8,5,9);break;
    case U_VPN:
        L(9,1,16,4);L(16,4,15,12);L(15,12,9,17);L(9,17,3,12);L(3,12,2,4);L(2,4,9,1);
        L(5,9,8,12);L(8,12,13,6);break;
    case U_SDWAN: B(6,0,6,5);L(9,5,9,10);L(3,10,15,10);L(3,10,3,13);L(15,10,15,13);B(0,13,6,5);B(12,13,6,5);break;
    case U_DEVICES: B(1,10,16,7);L(4,10,3,3);L(14,10,15,3);L(4,13,5,13);L(8,13,9,13);L(12,13,14,13);break;
    case U_ALERTS: L(4,13,4,6);L(4,6,6,3);L(6,3,12,3);L(12,3,14,6);L(14,6,14,13);L(2,14,16,14);L(7,17,11,17);L(9,1,9,3);break;
    case U_SITES:
        arc(c,x+size/2,y+size*6/18,size*5/18,180,359,1,col);
        L(4,6,4,10);L(4,10,9,17);L(9,17,14,10);L(14,10,14,6);
        arc(c,x+size/2,y+size*6/18,size*2/18,0,359,1,col);break;
    case U_SETTINGS:
        arc(c,x+size/2,y+size/2,size*6/18,0,359,1,col);
        arc(c,x+size/2,y+size/2,size*2/18,0,359,1,col);
        L(9,0,9,3);L(9,15,9,18);L(0,9,3,9);L(15,9,18,9);
        L(2,2,5,5);L(13,13,16,16);L(2,16,5,13);L(13,5,16,2);break;
    case I_WIFI:
        L(0,5,4,2);L(4,2,9,1);L(9,1,14,2);L(14,2,18,5);
        L(3,8,6,5);L(6,5,12,5);L(12,5,15,8);L(6,11,9,9);L(9,9,12,11);L(9,14,9,15);break;
    case I_BATTERY: B(1,4,14,10);L(17,7,17,11);L(4,7,4,11);L(7,7,7,11);L(10,7,10,11);break;
    case I_CHECK:
        arc(c,x+size/2,y+size/2,size*8/18,0,359,1,col);L(4,9,8,12);L(8,12,14,5);break;
    case I_KEY:
        arc(c,x+size*5/18,y+size*5/18,size*4/18,0,359,1,col);L(8,8,16,16);L(12,12,15,9);L(15,15,18,12);break;
    case I_ARROW_DOWN: L(9,1,9,16);L(5,12,9,16);L(9,16,13,12);break;
    case I_ARROW_UP: L(9,1,9,16);L(5,5,9,1);L(9,1,13,5);break;
    default: L(9,1,17,16);L(17,16,1,16);L(1,16,9,1);L(9,6,9,10);L(9,13,9,14);break;
    }
#undef L
#undef B
}
/* The renderer still clips using actual glyph advances. This approximate width
 * only centers short captions without allocating an LVGL object. */
static void caption(const unifi_canvas_t *c,int y,uint32_t color,const char *s)
{
    int width=0;for(const unsigned char *p=(const unsigned char *)s;*p;p++) {
        if(*p<128)width+=6;else if((*p&0xC0)!=0x80)width+=12;
    }
    if(width>212)width=212;
    if(width<1)width=1;
    txt(c,14+(212-width)/2,y,width,12,color,s);
}
static void missing(const unifi_canvas_t *c,const char *title,const char *note)
{
    icon(c,I_WARNING,102,113,36,AMBER);
    txt(c,24,170,192,14,FG,title);txt(c,24,198,192,12,MUTED,note);
}
static void date_label(char *b,size_t z,int64_t epoch,bool with_clock)
{
    if(epoch<1704067200) {snprintf(b,z,"时间未知");return;}
    time_t t=(time_t)(epoch+28800);struct tm tm;gmtime_r(&t,&tm);
    strftime(b,z,with_clock?"%m-%d %H:%M":"%m-%d",&tm);
}
static const char *wan_current_state(const unifi_wan_t *w)
{
    if(w->enabled_known&&!w->enabled)return "已停用";
    if(!strcmp(w->state,"online"))return "在线";
    if(!strcmp(w->state,"offline")||!strcmp(w->state,"disconnected"))return "离线";
    if(!strcmp(w->state,"connecting"))return "连接中";
    return "未知";
}
static bool wan_fresh(const unifi_snapshot_t *s,int64_t now)
{ return s->wans_available&&!s->wans_error[0]&&s->wans_epoch>0&&now>=s->wans_epoch&&now-s->wans_epoch<=120; }
static uint32_t wan_tone(const unifi_wan_t *w,bool fresh)
{ return !fresh?MUTED:!strcmp(wan_current_state(w),"在线")?GREEN:!strcmp(wan_current_state(w),"离线")?AMBER:MUTED; }
static void wan_availability(char *b,size_t size,const unifi_wan_t *w,int64_t now)
{
    if(w->history_epoch>0&&now>=w->history_epoch&&now-w->history_epoch<=120&&isfinite(w->availability_pct))snprintf(b,size,"%.1f%%",w->availability_pct);
    else snprintf(b,size,"--");
}
static void wan_details(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s,int64_t now)
{
    char b[64];unsigned count=unifi_wan_count(s);int i=unifi_wan_index(s,count?(unsigned)n->selection%count:0);
    if(i<0){txt(c,14,112,212,18,MUTED,"等待 WAN 线路数据");txt(c,14,155,212,12,AMBER,s->wans_error[0]?"线路查询异常，请稍后重试":"正在自动识别线路");return;}
    const unifi_wan_t *w=&s->wans[i];bool fresh=wan_fresh(s,now);
    snprintf(b,sizeof b,"WAN%d   %u/%u%s",i+1,(unsigned)n->selection%count+1,count,s->wans_complete?"":"+");
    txt(c,14,92,212,18,BLUE,b);
    pair(c,124,"互联网",fresh?wan_current_state(w):"上次状态");
    pair(c,154,"物理链路",w->link_known?(w->link_up?"已接通":"未接通"):"未知");
    wan_availability(b,sizeof b,w,now);pair(c,184,"历史可用率",b);
    snprintf(b,sizeof b,w->period_sec?"%.1f 小时":"未知",w->period_sec/3600.0);pair(c,214,"统计周期",b);
    if(s->wans_error[0])snprintf(b,sizeof b,"查询异常 · 缓存%s",wan_current_state(w));
    else if(!fresh)snprintf(b,sizeof b,"数据过期 · 缓存%s",wan_current_state(w));
    else snprintf(b,sizeof b,"查询 %lld 秒前%s",(long long)(now-s->wans_epoch),w->active?" · 当前出口":"");
    txt(c,14,246,212,12,fresh?MUTED:AMBER,b);
}
static void overview(const unifi_canvas_t *c,const unifi_snapshot_t *s,int64_t now)
{
    const unifi_site_t *st=site(s);char b[64];unsigned count=unifi_wan_count(s);bool fresh=wan_fresh(s,now);
    bool healthy=st->internet_issues_valid&&st->internet_issues==0;
    bool issues=st->internet_issues_valid&&st->internet_issues>0;
    if(fresh)for(unsigned i=0;i<UNIFI_WAN_MAX;i++){
        const unifi_wan_t *w=&s->wans[i];
        if(w->present&&!(w->enabled_known&&!w->enabled)&&(!strcmp(w->state,"offline")||!strcmp(w->state,"disconnected")||(w->link_known&&!w->link_up))){issues=true;healthy=false;}
    }
    uint32_t tone=healthy?GREEN:issues?AMBER:MUTED;
    icon(c,healthy?I_CHECK:I_WARNING,14,94,23,tone);
    txt(c,46,90,180,18,FG,s->demo&&healthy?"互联网正常":healthy?"近期无网络告警":issues?"线路需要关注":"网络状态待确认");
    txt(c,46,115,180,12,MUTED,st->isp_name[0]?st->isp_name:"等待运营商信息");
    if(s->wans_available)snprintf(b,sizeof b,"WAN 线路 %u%s 条",count,s->wans_complete?"":"+");
    else snprintf(b,sizeof b,"WAN 自动识别");
    txt(c,14,137,131,12,FG,b);txt(c,148,137,78,12,MUTED,"历史可用率");
    if(!count){txt(c,14,165,212,14,MUTED,"等待线路数据");txt(c,14,187,212,12,MUTED,"OK 查看线路详情");}
    else for(unsigned row=0;row<2&&row<count;row++){
        /* Cycle pairs for gateways with more than two WANs; details are manual. */
        unsigned sel=(unsigned)((now>0?now/5:0)%((count+1)/2))*2+row;if(sel>=count)break;
        int i=unifi_wan_index(s,sel);const unifi_wan_t *w=&s->wans[i];int y=162+(int)row*23;
        box(c,14,y+5,5,5,2,wan_tone(w,fresh));snprintf(b,sizeof b,"WAN%d",i+1);txt(c,25,y,48,12,FG,b);
        txt(c,78,y,64,12,wan_tone(w,fresh),fresh?wan_current_state(w):"待更新");
        wan_availability(b,sizeof b,w,now);txt(c,158,y,68,12,MUTED,b);
        box(c,14,y+17,212,2,0,EDGE);
        if(strcmp(b,"--")&&w->availability_pct>0){int width=(int)(212*w->availability_pct/100);if(width<1)width=1;box(c,14,y+17,width,2,0,BLUE);}
    }
    ln(c,14,207,226,207,1,EDGE);ln(c,119,216,119,260,1,EDGE);
    number(c,14,215,53,st->counts_valid?st->devices_online:-1,FG);
    snprintf(b,sizeof b,st->counts_valid?"/ %d":"/ --",st->devices_total);txt(c,69,225,42,12,MUTED,b);
    number(c,133,215,62,st->counts_valid?st->clients:-1,FG);txt(c,204,225,22,12,MUTED,"台");
    txt(c,14,249,100,12,MUTED,"在线设备");txt(c,133,249,93,12,MUTED,"在线客户端");
}
static void rate_hero(const unifi_canvas_t *c,int x,const char *label,double value,uint32_t col)
{
    char b[40];const char *unit="Mbps";
    if(!isfinite(value)||value<0)snprintf(b,sizeof b,"--");
    else {if(value>=1e9)unit="Gbps";snprintf(b,sizeof b,"%.2f",value/(!strcmp(unit,"Gbps")?1e9:1e6));}
    icon(c,col==CYAN?I_ARROW_DOWN:I_ARROW_UP,x,92,12,col);txt(c,x+17,91,83,12,MUTED,label);
    int font=strlen(b)>5?18:24;
    int width=(int)strlen(b)*(font==24?13:10)-6;if(width<24)width=24;if(width>65)width=65;
    txt(c,x,111,width,font,FG,b);txt(c,x+width+3,120,100-width-3,12,MUTED,unit);
}
static void graph(const unifi_canvas_t *c,const unifi_snapshot_t *s,bool latency)
{
    const int left=43,right=222,top=166,bottom=214;
    unsigned count=latency?s->history_count:s->live_count;if(count>UNIFI_HISTORY_POINTS)count=UNIFI_HISTORY_POINTS;
    const float *series[2]={latency?s->history_latency:s->live_down,latency?s->history_loss:s->live_up};
    double max=latency?1:1e6,secondary_max=5;bool seen=false;
    for(unsigned i=0;i<count;i++)for(int k=0;k<2;k++) {
        if(!isfinite(series[k][i])||series[k][i]<0)continue;
        seen=true;
        if(latency&&k==1) {if(series[k][i]>secondary_max)secondary_max=series[k][i];}
        else if(series[k][i]>max)max=series[k][i];
    }
    double base=pow(10,floor(log10(max))),mantissa=max/base;
    max=base*(mantissa<=2?2:mantissa<=5?5:10);
    txt(c,14,142,76,12,MUTED,latency?"ms":"Mbps");
    if(latency){char b[20];snprintf(b,sizeof b,"%.0f%%",secondary_max);txt(c,190,142,36,12,RED,b);}
    for(int i=0;i<3;i++) {
        int y=top+(bottom-top)*i/2;ln(c,left,y,right,y,1,EDGE);
        char b[24];double tick=max*(2-i)/2/(latency?1:1e6);
        snprintf(b,sizeof b,tick>0&&tick<10?"%.1f":"%.0f",tick);txt(c,14,y-7,26,12,MUTED,b);
    }
    for(int i=0;i<4;i++)ln(c,left+(right-left)*i/3,top,left+(right-left)*i/3,bottom,1,EDGE);
    if(count<2||!seen)txt(c,53,183,165,12,MUTED,"尚无可用趋势数据");
    else {
        unsigned n=count>32?32:count;
        for(unsigned i=1;i<n;i++) {
            unsigned a=(i-1)*(count-1)/(n-1),b=i*(count-1)/(n-1);
            for(int k=0;k<2;k++) {
                /* Missing buckets interrupt the line; never imply a zero or
                 * connect across an intermediate gap skipped by decimation. */
                bool valid=true;for(unsigned at=a;at<=b;at++){if(!isfinite(series[k][at])||series[k][at]<0)valid=false;if(!latency&&at>a&&s->live_epoch[at]-s->live_epoch[at-1]>120)valid=false;}
                if(!valid)continue;
                double scale=latency&&k?secondary_max:max;
                uint32_t color=latency?(k?RED:AMBER):(k?PURPLE:CYAN);
                ln(c,left+(i-1)*(right-left)/(n-1),bottom-(int)(series[k][a]/scale*(bottom-top)),left+i*(right-left)/(n-1),bottom-(int)(series[k][b]/scale*(bottom-top)),1,color);
            }
        }
    }
    if(latency){txt(c,43,219,58,12,MUTED,"前一天");txt(c,114,219,44,12,MUTED,"12小时");}
    else {char span[32];int64_t elapsed=count>1?s->live_epoch[count-1]-s->live_epoch[0]:0;snprintf(span,sizeof span,"最近 %lld 秒",(long long)elapsed);txt(c,43,219,135,12,MUTED,span);}
    txt(c,188,219,38,12,MUTED,"最近");
}
static void internet(const unifi_canvas_t *c,const unifi_snapshot_t *s,bool detail)
{
    char b[48];
    if(detail) {
        txt(c,14,91,100,12,AMBER,"平均延迟");txt(c,126,91,100,12,RED,"丢包率");
        snprintf(b,sizeof b,s->metrics_valid?"%.1f":"--",s->avg_latency_ms);txt(c,14,111,68,24,FG,b);txt(c,84,120,30,12,MUTED,"ms");
        snprintf(b,sizeof b,s->metrics_valid?"%.1f":"--",s->loss_pct);txt(c,126,111,68,24,FG,b);txt(c,196,120,30,12,MUTED,"%");
    } else {
        rate_hero(c,14,"当前下载",s->wan_valid?s->wan_down_bps:-1,CYAN);
        rate_hero(c,126,"当前上传",s->wan_valid?s->wan_up_bps:-1,PURPLE);
    }
    graph(c,s,detail);ln(c,14,243,226,243,1,EDGE);
    if(!detail){char stamp[24];date_label(stamp,sizeof stamp,s->metric_epoch,true);snprintf(b,sizeof b,"ISP 历史采样 %s",strlen(stamp)>6?stamp+6:stamp);txt(c,14,252,212,12,MUTED,b);return;}
    txt(c,14,252,48,12,MUTED,"峰值延迟");
    snprintf(b,sizeof b,s->metrics_valid?"%.1f ms":"--",detail?s->max_latency_ms:s->avg_latency_ms);txt(c,67,249,61,14,FG,b);
    txt(c,135,252,36,12,MUTED,detail?"可用率":"丢包");
    snprintf(b,sizeof b,(detail?site(s)->uptime_valid:s->metrics_valid)?"%.1f%%":"--",detail?site(s)->wan_uptime_pct:s->loss_pct);txt(c,174,249,52,14,FG,b);
}
static void traffic(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s)
{
    if(!s->demo){
        icon(c,U_INTERNET,102,108,36,BLUE);txt(c,14,163,212,18,FG,"七天流量暂不可用");
        txt(c,14,205,212,12,MUTED,"ISP 速率不是累计流量");txt(c,14,231,212,12,MUTED,"需接入经过验证的字节统计");return;
    }
    unsigned selected=(unsigned)n->selection%7;const unifi_day_t *d=&s->days[selected];char b[48];
    double max=1;
    for(int i=0;i<7;i++)if(s->days[i].valid&&s->days[i].down_bytes+s->days[i].up_bytes>max)max=s->days[i].down_bytes+s->days[i].up_bytes;
    snprintf(b,sizeof b,d->valid?"%.1f":"--",(d->down_bytes+d->up_bytes)/1e9);txt(c,14,90,87,24,FG,b);txt(c,102,101,24,12,MUTED,"GB");
    date_label(b,sizeof b,d->day_epoch,false);txt(c,166,100,60,12,FG,b);
    txt(c,14,122,24,12,MUTED,"GB");
    for(int k=0;k<3;k++) {
        int y=145+k*25;ln(c,41,y,226,y,1,EDGE);
        snprintf(b,sizeof b,"%.0f",max/1e9*(2-k)/2);txt(c,14,y-7,25,12,MUTED,b);
    }
    for(int i=0;i<7;i++) {
        const unifi_day_t *item=&s->days[i];int x=45+i*26;
        int dh=item->valid?(int)(item->down_bytes/max*51):0,uh=item->valid?(int)(item->up_bytes/max*51):0;
        if(dh>0)box(c,x,195-dh,14,dh,0,(unsigned)i==selected?CYAN:0x2D697C);
        if(uh>0)box(c,x,195-dh-uh,14,uh,0,(unsigned)i==selected?PURPLE:0x655074);
        if(!item->valid)ln(c,x,190,x+14,190,1,EDGE);
        if(item->valid&&item->samples<24)box(c,x+5,190-dh-uh,4,4,2,AMBER);
        date_label(b,sizeof b,item->day_epoch,false);const char *day=strchr(b,'-');
        if(!day)snprintf(b,sizeof b,"%d",i+1);
        txt(c,x-1,201,24,12,(unsigned)i==selected?FG:MUTED,day?day+1:b);
    }
    ln(c,14,225,226,225,1,EDGE);
    snprintf(b,sizeof b,d->valid?"下行 %.1f GB":"下行 --",d->down_bytes/1e9);txt(c,14,232,104,12,CYAN,b);
    snprintf(b,sizeof b,d->valid?"上行 %.1f GB":"上行 --",d->up_bytes/1e9);txt(c,126,232,100,12,PURPLE,b);
    snprintf(b,sizeof b,"覆盖 %u/24 小时 · %s",d->samples,d->samples<24?"数据不完整":"流量估算");txt(c,14,253,212,12,d->samples<24?AMBER:MUTED,b);
}
static const char *kind(const char *type)
{
    if(!strcmp(type,"WIRELESS"))return "无线";
    if(!strcmp(type,"WIRED"))return "有线";
    if(!strcmp(type,"TELEPORT"))return "Teleport";
    if(!strcmp(type,"VPN"))return "VPN";
    return "未知";
}
static void client_list(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s,int64_t now)
{
    const unifi_clients_t *v=&s->clients;
    if(!v->available){missing(c,"客户端接口不可用","保留站点统计，请检查权限");return;}
    unsigned begin=(unsigned)n->selection/3*3;
    for(unsigned i=0;i<3;i++) {
        unsigned absolute=begin+i;if(absolute<v->page_offset||absolute>=v->page_offset+v->returned)continue;
        const unifi_client_t *cl=&v->items[absolute-v->page_offset];int y=91+(int)i*55;
        if(absolute==(unsigned)n->selection)box(c,14,y-1,212,52,5,CARD);
        int symbol=!strcmp(cl->type,"VPN")||!strcmp(cl->type,"TELEPORT")?U_VPN:!strcmp(cl->type,"WIRELESS")?I_WIFI:U_CLIENTS;
        icon(c,symbol,17,y+7,17,BLUE);txt(c,42,y+2,179,14,FG,cl->name);
        txt(c,42,y+22,179,12,MUTED,cl->ip);
        char b[64],dur[32];unifi_duration(dur,sizeof dur,cl->connected_epoch>0&&now>=cl->connected_epoch?now-cl->connected_epoch:-1);
        snprintf(b,sizeof b,"%s · 在线 %s",kind(cl->type),dur);txt(c,42,y+38,179,12,MUTED,b);
        ln(c,14,y+54,226,y+54,1,EDGE);
    }
    if(!v->returned)txt(c,24,148,192,14,MUTED,"暂无在线客户端");
    char b[56];int total=n->page==U_VPN?v->vpn+v->teleport:v->total;
    snprintf(b,sizeof b,"%d / %d%s",total>0?n->selection+1:0,total<0?0:total,(n->page==U_VPN?v->vpn_complete:v->complete)?" · 在线名单":" · 部分结果");caption(c,258,MUTED,b);
}
static void clients(const unifi_canvas_t *c,const unifi_snapshot_t *s)
{
    const unifi_clients_t *v=&s->clients;int total=v->available?v->total:(site(s)->counts_valid?site(s)->clients:-1);
    arc(c,64,140,41,0,359,8,EDGE);
    if(v->available&&total>0) {
        int values[3]={v->wireless,v->wired,v->vpn+v->teleport};uint32_t colors[3]={BLUE,CYAN,PURPLE};int start=270,remaining=360;
        for(int i=0;i<3;i++) {
            int angle=values[i]>0?(int)((double)values[i]*360/total):0;if(angle>remaining)angle=remaining;
            if(angle>3)arc(c,64,140,41,start,start+angle-3,8,colors[i]);
            start+=angle;remaining-=angle;
        }
    }
    char b[24];snprintf(b,sizeof b,total<0?"--":"%d",total);int digitw=(int)strlen(b)*14;if(digitw>70)digitw=70;
    txt(c,64-digitw/2,120,digitw,24,FG,b);txt(c,52,153,28,12,MUTED,"在线");
    const char *labels[3]={"无线","有线","VPN"};int values[3]={v->wireless,v->wired,v->vpn};uint32_t colors[3]={BLUE,CYAN,PURPLE};
    for(int i=0;i<3;i++) {int y=104+i*28;box(c,126,y+3,5,12,2,colors[i]);txt(c,137,y,51,14,FG,labels[i]);snprintf(b,sizeof b,v->available?"%d":"--",values[i]);txt(c,194,y,32,14,FG,b);}
    ln(c,14,202,226,202,1,EDGE);
    txt(c,14,220,33,12,MUTED,"访客");number(c,51,211,54,v->available?v->guest:-1,FG);
    txt(c,126,220,53,12,MUTED,"Teleport");number(c,184,211,42,v->available?v->teleport:-1,FG);
    txt(c,14,249,212,12,MUTED,v->available?(v->complete?"访客已计入总数 · OK 查看名单":"分类为部分统计，查询未完成"):"分类不可用 · 保留站点总数");
}
static void vpn(const unifi_canvas_t *c,const unifi_snapshot_t *s,int64_t now)
{
    const unifi_clients_t *v=&s->clients;char b[64],dur[32];
    number(c,14,90,43,v->available&&v->vpn_complete?v->vpn:-1,FG);txt(c,60,101,70,14,FG,"在线 VPN");
    snprintf(b,sizeof b,v->available&&v->vpn_complete?"Teleport %d":"Teleport --",v->teleport);txt(c,145,101,81,12,MUTED,b);
    panel(c,14,133,212,108,EDGE,CARD);
    txt(c,25,142,90,12,MUTED,"最近上线");txt(c,163,142,52,12,v->latest_vpn_valid?GREEN:MUTED,v->latest_vpn_valid?"当前在线":"暂无记录");
    txt(c,25,164,190,18,FG,v->latest_vpn_valid?v->latest_vpn.name:"暂无可用连接");
    txt(c,25,192,190,12,MUTED,v->latest_vpn_valid?v->latest_vpn.ip:"仅统计当前在线会话");
    unifi_duration(dur,sizeof dur,v->latest_vpn_valid&&v->latest_vpn.connected_epoch>0&&now>=v->latest_vpn.connected_epoch?now-v->latest_vpn.connected_epoch:-1);
    char stamp[24];date_label(stamp,sizeof stamp,v->latest_vpn_valid?v->latest_vpn.connected_epoch:0,true);
    snprintf(b,sizeof b,"%s · %s",stamp,dur);txt(c,25,215,190,12,MUTED,b);
    txt(c,14,251,212,12,MUTED,"OK 查看全部在线名单与时长");
}
static const char *wan_state(const char *value)
{
    if(!strcmp(value,"no_issues"))return "无告警";
    if(!strcmp(value,"issues"))return "异常";
    if(!strcmp(value,"configured"))return "已配置";
    return "未知";
}
static void sdwan(const unifi_canvas_t *c,const unifi_snapshot_t *s,bool detail)
{
    const unifi_sdwan_t *v=&s->sdwan;char b[72];bool is_mesh=!strcmp(v->type,"Mesh");
    bool available=v->available;
    if(detail) {
        pair(c,94,"组网模式",is_mesh?"Mesh 对等互联":!strcmp(v->type,"sdwan-hbsp")?"Hub / Spoke":"未知");
        snprintf(b,sizeof b,available?"%d / %d":"未知",v->connected,v->total);pair(c,123,is_mesh?"已连接 / 总链路":"已连接 / 总隧道",b);
        snprintf(b,sizeof b,available&&v->primary_latency_valid?"%s %.0fms":"%s",available?wan_state(v->primary):"未知",v->primary_latency_ms);pair(c,152,"主 WAN",is_mesh?"接口未提供":b);
        snprintf(b,sizeof b,available&&v->backup_latency_valid?"%s %.0fms":"%s",available?wan_state(v->backup):"未知",v->backup_latency_ms);pair(c,181,"备用 WAN",is_mesh?"接口未提供":b);
        if(is_mesh)snprintf(b,sizeof b,"%d",v->peers);else snprintf(b,sizeof b,available?"%d / %d":"未知",v->hubs,v->spokes);pair(c,210,is_mesh?"组网站点":"Hub / Spoke",b);
        snprintf(b,sizeof b,available?"错误 %d · 警告 %d":"API 未覆盖时显示未知",v->errors,v->warnings);txt(c,14,247,212,12,available?MUTED:AMBER,b);return;
    }
    bool connected=available&&v->total>0&&v->connected==v->total;
    txt(c,14,91,103,18,connected?GREEN:AMBER,!available?"状态未知":connected?"已连接":"连接待检查");
    snprintf(b,sizeof b,available?(is_mesh?"%d / %d 链路":"%d / %d 隧道"):"接口待验证",v->connected,v->total);txt(c,126,97,100,12,MUTED,b);
    for(int y=135;y<195;y+=6)ln(c,21,y,21,y+3,1,available?BLUE:EDGE);
    const char *remote="远端站点";
    for(unsigned j=0;j<s->site_count;j++)if(!strcmp(s->sites[j].host_id,v->remote_host)&&!strcmp(s->sites[j].site_id,v->remote_site))remote=s->sites[j].name;
    for(int i=0;i<2;i++) {
        int y=124+i*54;box(c,18,y+10,7,7,3,connected?GREEN:MUTED);panel(c,35,y,191,47,EDGE,CARD);
        const char *name=unifi_site_display_name(i==0?(site(s)->name[0]?site(s)->name:"当前站点"):(s->demo&&s->site_count>1?s->sites[site(s)==&s->sites[0]?1:0].name:remote));
        txt(c,45,y+5,171,14,FG,name);
        txt(c,45,y+26,171,12,MUTED,s->demo?(i==0?"办公室 · 模拟站点":"家中 · 模拟站点"):i==0?"当前监控站点":is_mesh?"Mesh 互联端点":"远端站点");
    }
    for(int i=0;i<2;i++) {
        int x=i?124:14;panel(c,x,235,102,34,EDGE,CARD);
        txt(c,x+6,244,33,12,MUTED,is_mesh?(i?"警告":"错误"):(i?"备用":"主线"));
        if(is_mesh){snprintf(b,sizeof b,available?"%d":"--",i?v->warnings:v->errors);txt(c,x+41,244,55,12,FG,b);}
        else txt(c,x+41,244,55,12,FG,available?wan_state(i?v->backup:v->primary):"未知");
    }
}
static void uptime_hero(const unifi_canvas_t *c,int64_t seconds)
{
    if(seconds<0){txt(c,14,93,212,18,MUTED,"运行时间未知");return;}
    const int64_t values[3]={seconds/86400,seconds/3600%24,seconds/60%60};
    const char *units[3]={"天","时","分"};int x=14;
    for(int i=0;i<3;i++) {
        char b[24];snprintf(b,sizeof b,"%lld",(long long)values[i]);
        int width=(int)strlen(b)*14;if(width>56)width=56;
        txt(c,x,90,width,24,FG,b);x+=width+2;txt(c,x,101,14,14,MUTED,units[i]);x+=19;
    }
}
static void devices(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s)
{
    /* Prefix plus the complete 63-byte UTF-8 name, before pixel-width clipping. */
    char b[96];
    if(n->mode==U_DETAIL&&s->device_count) {
        const unifi_device_t *d=&s->devices[(unsigned)n->selection%s->device_count];
        if(n->rates) {
            icon(c,U_DEVICES,14,94,18,BLUE);txt(c,42,91,184,14,FG,d->name);
            unifi_rate(b,sizeof b,d->rates_valid?d->rx_bps:-1,true);metric(c,14,128,212,"上联接收 · API Bps",b,CYAN);
            unifi_rate(b,sizeof b,d->rates_valid?d->tx_bps:-1,true);metric(c,14,184,212,"上联发送 · API Bps",b,PURPLE);
            double sum=d->rx_bps+d->tx_bps;box(c,14,237,212,8,4,EDGE);
            if(d->rates_valid&&isfinite(sum)&&sum>0) {int w=(int)(d->rx_bps/sum*212);if(w<0)w=0;if(w>212)w=212;box(c,14,237,w,8,3,CYAN);box(c,14+w,237,212-w,8,3,PURPLE);}
            txt(c,14,253,212,12,MUTED,"收发比例 · 上下键切换设备");
        } else {
            unsigned begin=(unsigned)n->selection/3*3;
            for(unsigned i=0;i<3&&begin+i<s->device_count;i++) {
                const unifi_device_t *item=&s->devices[begin+i];int y=92+(int)i*55;
                if(begin+i==(unsigned)n->selection)box(c,14,y-2,212,51,5,CARD);
                icon(c,U_DEVICES,17,y+7,17,BLUE);txt(c,42,y,179,14,FG,item->name);
                date_label(b,sizeof b,item->startup_epoch,true);txt(c,42,y+22,122,12,MUTED,b);
                txt(c,171,y+22,50,12,online(item->state)?GREEN:AMBER,online(item->state)?"在线":!strcmp(item->state,"offline")||!strcmp(item->state,"OFFLINE")?"离线":"未知");
                ln(c,14,y+49,226,y+49,1,EDGE);
            }
            snprintf(b,sizeof b,"设备 %d / %u · 北京时间",n->selection+1,s->device_count);caption(c,258,MUTED,b);
        }
        return;
    }
    const unifi_device_t *d=&s->devices[0];uptime_hero(c,s->device_count&&d->uptime_valid?d->uptime_sec:-1);
    date_label(b,sizeof b,d->startup_epoch,true);txt(c,14,128,108,12,MUTED,s->device_count?d->name:"等待设备信息");txt(c,128,128,98,12,MUTED,b);
    int count=s->device_total,off=s->device_offline;
    txt(c,14,161,120,12,FG,"设备在线");snprintf(b,sizeof b,s->devices_complete?"%d / %d":"-- / --",count-off,count);txt(c,163,161,63,12,MUTED,b);
    int blocks=count>12?12:count;if(blocks<1)blocks=11;
    for(int i=0;i<blocks;i++) {int x=14+i*215/blocks,next=14+(i+1)*215/blocks;uint32_t col=!s->devices_complete?EDGE:(count>0&&i<(count-off)*blocks/count)?GREEN:AMBER;box(c,x,184,next-x-3,12,2,col);}
    const char *offline_name=NULL;for(unsigned i=0;i<s->device_count;i++)if(!strcmp(s->devices[i].state,"offline")||!strcmp(s->devices[i].state,"OFFLINE")){offline_name=s->devices[i].name;break;}
    if(!s->devices_complete)snprintf(b,sizeof b,"设备统计未完成");else if(offline_name)snprintf(b,sizeof b,"离线 · %s",offline_name);else snprintf(b,sizeof b,off?"%d 台设备离线":"所有设备在线",off);
    txt(c,14,204,212,12,off?AMBER:MUTED,b);ln(c,14,230,226,230,1,EDGE);
    for(int i=0;i<2;i++) {
        int x=i?128:14;bool valid=s->device_count&&(i?d->memory_valid:d->cpu_valid);double value=i?d->memory_pct:d->cpu_pct;
        txt(c,x,239,45,12,MUTED,i?"内存":"CPU");snprintf(b,sizeof b,valid?"%.0f%%":"--",value);txt(c,x+55,239,43,12,MUTED,b);
        box(c,x,261,98,5,2,EDGE);if(valid&&isfinite(value)&&value>0){if(value>100)value=100;box(c,x,261,(int)(98*value/100),5,2,BLUE);}
    }
}
static void alerts(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_alerts_t *a)
{
    if(!a->count){icon(c,I_CHECK,101,118,38,GREEN);txt(c,24,176,192,14,FG,"暂无本机检测到的变化");txt(c,24,203,192,12,MUTED,"首次连接只建立状态基线");return;}
    char b[48];snprintf(b,sizeof b,"%u 条记录",a->count);txt(c,14,91,100,14,AMBER,b);txt(c,126,94,100,12,MUTED,"设备观察记录");
    unsigned begin=n->mode==U_DETAIL?(unsigned)n->selection/3*3:0;
    for(unsigned i=0;i<3&&begin+i<a->count;i++) {
        int y=119+(int)i*50;const unifi_alert_t *item=&a->items[begin+i];
        int which=strstr(item->text,"VPN")?U_VPN:strstr(item->text,"Wi-Fi")?I_WIFI:U_ALERTS;
        icon(c,which,14,y+4,17,i?BLUE:AMBER);txt(c,40,y,186,14,i?FG:AMBER,item->text);
        date_label(b,sizeof b,item->epoch,true);txt(c,40,y+22,186,12,MUTED,b);ln(c,14,y+44,226,y+44,1,EDGE);
    }
}
static void sites(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s)
{
    if(!s->site_count){
        missing(c,s->refreshing?"正在查询站点":s->error[0]?"站点查询未完成":"尚未获取站点",
                s->error[0]?"设置菜单中查看连接诊断":s->refreshing?"结果返回后自动更新":"请在设置中配置 API");return;
    }
    int selected=n->mode==U_DETAIL?n->selection:s->selected_site;if(selected<0||selected>=s->site_count)selected=0;
    int begin=selected/2*2;
    for(int i=0;i<2&&begin+i<s->site_count;i++) {
        int index=begin+i,y=92+i*88;const unifi_site_t *st=&s->sites[index];bool active=index==s->selected_site;
        panel(c,14,y,212,78,index==selected?0x426F9D:EDGE,CARD);icon(c,U_SITES,23,y+8,16,index==selected?BLUE:MUTED);
        char b[48];if(s->demo&&index<2)snprintf(b,sizeof b,"%s",index?"家中":"办公室");else snprintf(b,sizeof b,"站点 %d",index+1);
        txt(c,45,y+7,100,14,FG,b);txt(c,165,y+9,51,12,active?GREEN:MUTED,active?"已选中":"可切换");
        txt(c,24,y+31,192,14,FG,unifi_site_display_name(st->name));
        snprintf(b,sizeof b,st->counts_valid?"%d 客户端 · %d 设备在线":"统计等待更新",st->clients,st->devices_online);txt(c,24,y+54,192,12,MUTED,b);
    }
}
static void settings(const unifi_canvas_t *c,const unifi_connection_t *v,const unifi_snapshot_t *s)
{
    (void)s;
    int symbols[3]={I_WIFI,I_KEY,U_SETTINGS};const char *labels[3]={"Wi-Fi","UniFi 配置","刷新 / 亮度"};
    char setting[40];snprintf(setting,sizeof setting,"%us / %u%%",(unsigned)v->poll_seconds,(unsigned)v->brightness);
    const char *values[3]={v->wifi_connected?"已连接":"未连接",v->configured?"已保存":"未配置",setting};
    for(int i=0;i<3;i++) {
        int y=93+i*33;icon(c,symbols[i],14,y+2,16,i?MUTED:BLUE);
        txt(c,41,y,i==2?90:112,14,FG,labels[i]);
        txt(c,i==2?139:162,y+2,i==2?87:64,12,i?MUTED:v->wifi_connected?GREEN:AMBER,values[i]);
    }
    char idle[40];
    if(v->screen_timeout_sec)snprintf(idle,sizeof idle,"自动息屏 %u 秒",(unsigned)v->screen_timeout_sec);
    else snprintf(idle,sizeof idle,"自动息屏 已关闭");
    txt(c,14,184,212,12,MUTED,idle);
    panel(c,14,209,212,55,EDGE,CARD);txt(c,25,216,190,14,FG,"手机打开配置网页");txt(c,25,241,190,12,MUTED,"OK 进入 Wi-Fi 与 API 配置");
}
static void portal(const unifi_canvas_t *c,const unifi_connection_t *v)
{
    txt(c,14,91,212,12,MUTED,"连接设备热点");txt(c,14,110,212,18,FG,v->ap_ssid[0]?v->ap_ssid:"正在开启…");
    txt(c,14,142,212,12,MUTED,"热点密码");txt(c,14,160,212,24,BLUE,v->ap_password);
    panel(c,14,199,212,65,EDGE,CARD);txt(c,25,208,190,18,BLUE,"192.168.4.1");
    txt(c,25,239,190,12,MUTED,"手机浏览器打开设置");
}
static void footer(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s,const unifi_connection_t *conn,int64_t now)
{
    char b[80];uint32_t color=MUTED;
    if(n->mode==U_MENU)snprintf(b,sizeof b,"上下选择 · OK 确认 · 长按返回");
    else if(n->mode==U_PORTAL)snprintf(b,sizeof b,"连接热点后打开配置网页");
    else if(n->mode==U_INFO)snprintf(b,sizeof b,"OK 或长按返回");
    else if(n->mode==U_DETAIL)snprintf(b,sizeof b,"%s · 长按返回",n->page==U_OVERVIEW?(n->wan_details?"上下选择 WAN":"上下选择日期"):n->page==U_SITES?"上下选择 · OK 切换":"上下浏览 · OK 返回");
    else {
        bool live=n->page==U_INTERNET;bool links=n->page==U_OVERVIEW;
        int64_t epoch=links?s->wans_epoch:live?s->wan_epoch:n->page==U_SDWAN?s->sdwan.updated_epoch:(n->page==U_CLIENTS||n->page==U_VPN)?s->clients_epoch:s->fetched_epoch;
        char stamp[24];date_label(stamp,sizeof stamp,epoch,true);
        if(links&&epoch>0&&now>epoch+120){snprintf(b,sizeof b,"线路数据过期 · 等待更新");color=AMBER;}
        else if(links&&s->refreshing&&epoch>0)snprintf(b,sizeof b,"刷新中 · 上次 %lld 秒前",(long long)(now>=epoch?now-epoch:0));
        else if(s->refreshing)snprintf(b,sizeof b,"正在刷新 · 保留当前数据");
        else if(!s->demo&&!conn->wifi_connected) {snprintf(b,sizeof b,conn->configured?"Wi-Fi 断开 · 等待重新连接":"请先配置 Wi-Fi 与 API");color=AMBER;}
        else if(links&&s->wans_error[0]){snprintf(b,sizeof b,"线路查询失败 · 保留上次数据");color=AMBER;}
        else if(live&&s->wan_error[0]){snprintf(b,sizeof b,"速率查询失败 · 保留上次数据");color=AMBER;}
        else if(n->page==U_VPN&&!s->clients.vpn_complete){snprintf(b,sizeof b,"VPN 查询不完整 · 查看诊断");color=AMBER;}
        else if(n->page==U_SDWAN&&s->sdwan.error[0]){snprintf(b,sizeof b,"链路查询异常 · 查看诊断");color=AMBER;}
        else if(!s->demo&&!s->api_ok&&!live&&!links&&n->page!=U_VPN&&n->page!=U_SDWAN) {snprintf(b,sizeof b,"%s",s->connection_ok&&s->fetched_epoch>0?"部分数据失败 · 查看连接诊断":"API 异常 · 显示已有数据");color=AMBER;}
        else if(epoch>0&&now>epoch+120) {snprintf(b,sizeof b,"数据过期 · %lld 分钟前",(long long)((now-epoch)/60));color=AMBER;}
        else if(links)snprintf(b,sizeof b,epoch?"线路更新 %lld 秒前":"等待线路查询",(long long)(epoch&&now>=epoch?now-epoch:0));
        else if(live)snprintf(b,sizeof b,"速率查询 %lld 秒前",(long long)(epoch&&now>=epoch?now-epoch:0));
        else snprintf(b,sizeof b,"%s · %s",s->demo?"示例快照":"更新",stamp);
    }
    caption(c,278,color,b);
    int x=72;for(int i=0;i<U_PAGE_COUNT;i++){int width=i==(int)n->page?16:4;box(c,x,302,width,4,2,i==(int)n->page?BLUE:0x46505C);x+=width+6;}
}
void unifi_view(const unifi_canvas_t *c,const unifi_nav_t *n,const unifi_snapshot_t *s,
                const unifi_connection_t *conn,const unifi_alerts_t *a,int battery,int64_t now)
{
    static const char *const subtitles[U_PAGE_COUNT]={"WAN 当前状态 · 自动识别","WAN 当前速率 · 本机采样趋势","连接方式 · 在线客户端","最新上线 · 当前在线VPN","站点互联 · 隧道状态","网关运行时长","只记录本机观察到的变化","办公室与家中网络","设备内置配置网页"};
    box(c,0,0,240,320,0,BG);char b[64];
    txt(c,20,12,92,12,MUTED,site(s)->name[0]?unifi_site_display_name(site(s)->name):"AI Passport");
    unifi_clock_text(b,sizeof b,now,conn->time_valid);txt(c,116,12,38,12,conn->time_valid?FG:MUTED,b);
    icon(c,I_WIFI,160,14,12,conn->wifi_connected?MUTED:AMBER);icon(c,I_BATTERY,177,14,12,battery>=0&&battery<15?AMBER:MUTED);
    snprintf(b,sizeof b,battery<0?"--":"%d%%",battery);txt(c,194,12,32,12,battery>=0&&battery<15?AMBER:MUTED,b);
    icon(c,n->page,14,40,18,BLUE);
    const char *title=n->page==U_OVERVIEW&&n->mode==U_DETAIL&&n->wan_details?"WAN 线路详情":n->mode==U_MENU?(n->page==U_SETTINGS?"设备设置":"页面操作"):n->mode==U_PORTAL?"手机配网":n->mode==U_INFO?(n->page==U_SETTINGS?"连接诊断":"SD-WAN 重连"):unifi_page_title(n->page);
    txt(c,39,37,143,18,FG,title);snprintf(b,sizeof b,"%02d / 09",n->page+1);txt(c,184,43,42,12,MUTED,b);
    const char *sub=n->mode==U_MENU?unifi_page_title(n->page):n->mode==U_PORTAL?"手机连接热点，再打开网页":n->mode==U_INFO?(n->page==U_SETTINGS?"设备连接与时间状态":"官方接口能力说明"):n->mode==U_DETAIL?(n->page==U_OVERVIEW?(n->wan_details?"当前链路与历史统计":"七天流量 · 北京时间"):n->page==U_INTERNET?"延迟与丢包 · 近期指标":n->page==U_VPN?"全部在线 · IP 与在线时长":n->page==U_CLIENTS?"客户端名单 · 每屏3条":n->page==U_DEVICES?(n->rates?"设备上联 · 接收与发送":"主要设备 · 启动时间"):subtitles[n->page]):subtitles[n->page];
    txt(c,14,65,174,12,MUTED,sub);
    if(s->demo){box(c,192,64,34,18,3,0x29496C);box(c,193,65,32,16,2,BG);txt(c,197,65,26,12,BLUE,"演示");}
    if(n->mode==U_MENU) {
        for(int i=0;i<unifi_menu_count(n->page);i++) {
            int y=91+i*36;if(i==n->menu)panel(c,14,y,212,31,0x426F9D,0x1D3550);
            txt(c,23,y+6,194,14,i==n->menu?0xB7D8FF:FG,unifi_menu_title(n->page,i));
        }
    } else if(n->mode==U_PORTAL)portal(c,conn);
    else if(n->mode==U_INFO) {
        if(n->page==U_SETTINGS) {
            pair(c,94,"Wi-Fi",conn->wifi_connected?"已连接":"未连接");
            pair(c,124,"本机 IP",conn->ip[0]?conn->ip:"未知");
            pair(c,154,"时间同步",conn->time_valid?"已完成":"等待同步");
            if(s->demo)snprintf(b,sizeof b,"演示模式");
            else if(s->refreshing)snprintf(b,sizeof b,"正在查询");
            else if(s->http_status)snprintf(b,sizeof b,"HTTP %d",s->http_status);
            else snprintf(b,sizeof b,"%s",s->error[0]?"查询未完成":"等待查询");
            pair(c,184,"API 状态",b);
            txt(c,14,218,212,12,MUTED,"最近错误");
            txt(c,14,241,212,12,AMBER,conn->error[0]?conn->error:s->error[0]?s->error:"暂无错误");
        } else {
            icon(c,I_WARNING,14,94,21,AMBER);txt(c,44,92,182,18,AMBER,"暂不支持协议重连");
            txt(c,14,135,212,14,FG,"官方 API 没有此操作");txt(c,14,171,212,12,MUTED,"请在 UniFi 控制台检查连接");
            txt(c,14,196,212,12,MUTED,"协议重连不同于重启整台网关");txt(c,14,239,212,12,MUTED,"未发送任何重启请求");
        }
    } else switch(n->page) {
    case U_OVERVIEW:if(n->mode==U_DETAIL){if(n->wan_details)wan_details(c,n,s,now);else traffic(c,n,s);}else overview(c,s,now);break;
    case U_INTERNET:internet(c,s,n->mode==U_DETAIL);break;
    case U_CLIENTS:if(n->mode==U_DETAIL)client_list(c,n,s,now);else clients(c,s);break;
    case U_VPN:if(n->mode==U_DETAIL)client_list(c,n,s,now);else vpn(c,s,now);break;
    case U_SDWAN:sdwan(c,s,n->mode==U_DETAIL);break;
    case U_DEVICES:devices(c,n,s);break;
    case U_ALERTS:alerts(c,n,a);break;
    case U_SITES:sites(c,n,s);break;
    case U_SETTINGS:settings(c,conn,s);break;
    default:break;
    }
    footer(c,n,s,conn,now);
}
