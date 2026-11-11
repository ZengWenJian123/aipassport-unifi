#include "unifi_view.h"
#include "unifi_fixture.h"
#include "unifi_site_names.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { int x,y,w,h; const char *text; } region_t;
static region_t texts[80];static int count,frame;
static bool saw_diagnostics, saw_reconnect, saw_mesh, saw_current, saw_unavailable_traffic;
static char header_text[64], clock_text[8], api_state[48];
static void text(void *ctx,int x,int y,int w,int font,uint32_t col,const char *v)
{
    (void)ctx;(void)col;
    saw_mesh |= !strcmp(v,"1 / 1 链路");
    saw_current |= !strcmp(v,"当前下载");
    saw_unavailable_traffic |= !strcmp(v,"七天流量暂不可用");
    saw_diagnostics |= !strcmp(v,"连接诊断");
    saw_reconnect |= !strcmp(v,"暂不支持协议重连");
    if(x==20&&y==12)snprintf(header_text,sizeof header_text,"%s",v);
    if(x==116&&y==12)snprintf(clock_text,sizeof clock_text,"%s",v);
    if(x==110&&y==184)snprintf(api_state,sizeof api_state,"%s",v);
    int h=font==12?16:font==14?18:font==18?22:28;
    assert(x>=14&&y>=0&&x+w<=226&&y+h<=320);
    for(int i=0;i<count;i++) {
        region_t *r=&texts[i];
        if(x<r->x+r->w&&x+w>r->x&&y<r->y+r->h&&y+h>r->y) {
            fprintf(stderr,"frame %d text overlap [%d,%d,%d,%d] with [%d,%d,%d,%d]\n",frame,x,y,w,h,r->x,r->y,r->w,r->h);assert(0);
        }
    }
    assert(count<80);texts[count++]=(region_t){x,y,w,h,NULL};
}
static void rect(void *v,int x,int y,int w,int h,int r,uint32_t c)
{(void)v;(void)r;(void)c;assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=240&&y+h<=320);}
static void line(void *v,int x,int y,int xx,int yy,int w,uint32_t c)
{(void)v;(void)w;(void)c;assert(x>=0&&y>=0&&xx>=0&&yy>=0&&x<240&&xx<240&&y<320&&yy<320);}
static void arc(void *v,int x,int y,int r,int a,int b,int w,uint32_t c)
{(void)v;(void)a;(void)b;(void)w;(void)c;assert(x-r>=0&&x+r<240&&y-r>=0&&y+r<320);}
static void render(unifi_nav_t *n,unifi_snapshot_t *s,unifi_connection_t *c)
{
    unifi_alerts_t a={.count=8};for(int i=0;i<8;i++){strcpy(a.items[i].text,"设备在线状态发生变化");a.items[i].epoch=s->fetched_epoch;}
    unifi_canvas_t out={NULL,text,rect,line,arc};count=0;frame++;
    saw_diagnostics=saw_reconnect=saw_mesh=saw_current=saw_unavailable_traffic=false;
    unifi_view(&out,n,s,c,&a,9,1794067260);
}
int main(void)
{
    /* Match the BSP's delayed single click after release, as well as drivers
     * which deliver click first. Every wake gesture must be consumed. */
    for(unsigned button=0;button<3;button++)for(int release_first=0;release_first<2;release_first++) {
        unifi_screen_idle_t idle={.last_activity_ms=1000};
        unifi_screen_tick(&idle,60,60999);assert(!idle.asleep);
        unifi_screen_tick(&idle,60,61000);assert(idle.asleep);
        assert(!unifi_screen_input(&idle,button,U_PRESS,62000));assert(!idle.asleep);
        assert(!unifi_screen_input(&idle,button,U_LONG,64000));
        if(release_first)assert(!unifi_screen_input(&idle,button,U_RELEASE,65000));
        assert(!unifi_screen_input(&idle,button,U_CLICK,65100));
        if(!release_first)assert(!unifi_screen_input(&idle,button,U_RELEASE,65200));
        assert(!idle.pressed_mask);
        assert(!unifi_screen_input(&idle,button,U_PRESS,66000));
        unifi_screen_tick(&idle,30,100000);assert(!idle.asleep); // held button
        assert(!unifi_screen_input(&idle,button,U_RELEASE,101000));
        assert(unifi_screen_input(&idle,button,U_CLICK,101100)); // fresh press navigates
        unifi_screen_tick(&idle,30,131099);assert(!idle.asleep);
        unifi_screen_tick(&idle,30,131100);assert(idle.asleep);
        unifi_screen_tick(&idle,0,200000);assert(!idle.asleep); // always on
        unifi_screen_tick(&idle,60,100);assert(idle.last_activity_ms==100);
    }
    unifi_screen_idle_t missed={.asleep=true};
    assert(!unifi_screen_input(&missed,2,U_CLICK,1));assert(!missed.asleep);
    char clock[8];
    unifi_clock_text(clock,sizeof clock,1794067260,true);assert(!strcmp(clock,"00:01"));
    unifi_clock_text(clock,sizeof clock,1794067140,true);assert(!strcmp(clock,"23:59"));
    unifi_clock_text(clock,sizeof clock,1794067260,false);assert(!strcmp(clock,"--:--"));
    unifi_clock_text(clock,sizeof clock,0,true);assert(!strcmp(clock,"--:--"));
    /* A fast new-config result must not wait for the periodic UI tick. An old
     * request finishing after a newer save must never replace that result. */
    assert(unifi_generation_current_or_newer(2,1));
    assert(unifi_generation_current_or_newer(2,2));
    assert(!unifi_generation_current_or_newer(1,2));
    assert(unifi_generation_current_or_newer(0,UINT32_MAX));
    assert(!unifi_generation_current_or_newer(UINT32_MAX,0));
    assert(!strcmp(unifi_site_display_name("Demo Office"),"Demo Office"));
    assert(!strcmp(unifi_site_display_name("Demo Home"),"Demo Home"));
    assert(!strcmp(unifi_site_display_name("Another site"),"Another site"));
    unifi_snapshot_t s;unifi_connection_t c;unifi_fixture(&s,&c);
    unifi_nav_t clock_nav={0};render(&clock_nav,&s,&c);assert(!strcmp(clock_text,"00:01"));
    s.metric_epoch=0;render(&clock_nav,&s,&c);assert(!strcmp(clock_text,"00:01"));
    c.time_valid=false;render(&clock_nav,&s,&c);assert(!strcmp(clock_text,"--:--"));
    unifi_fixture(&s,&c);
    unifi_nav_t n={0};assert(unifi_navigate(&n,U_UP,&s)==U_NONE&&n.page==U_SETTINGS);
    unifi_navigate(&n,U_DOWN,&s);assert(n.page==U_OVERVIEW);
    unifi_navigate(&n,U_OK,&s);assert(n.mode==U_MENU);
    unifi_navigate(&n,U_DOWN,&s);unifi_navigate(&n,U_OK,&s);assert(n.mode==U_DETAIL&&!n.wan_details);
    unifi_navigate(&n,U_UP,&s);assert(n.selection==6);
    unifi_navigate(&n,U_BACK,&s);assert(n.mode==U_HOME);
    n=(unifi_nav_t){.page=U_OVERVIEW,.mode=U_MENU};
    unifi_navigate(&n,U_OK,&s);assert(n.wan_details&&n.mode==U_DETAIL);
    assert(unifi_wan_count(&s)==2);unifi_navigate(&n,U_UP,&s);assert(n.selection==1);
    unifi_navigate(&n,U_DOWN,&s);assert(n.selection==0);
    s.wans[1].present=false;s.wans[7]=s.wans[0];
    assert(unifi_wan_index(&s,1)==7&&unifi_wan_index(&s,2)==-1);
    render(&n,&s,&c);n.selection=1;render(&n,&s,&c);
    s.wans_epoch=1;n.mode=U_HOME;render(&n,&s,&c);
    strcpy(s.wans_error,"timeout");render(&n,&s,&c);
    unifi_fixture(&s,&c);
    n.page=U_SDWAN;n.mode=U_MENU;n.menu=2;
    assert(unifi_navigate(&n,U_OK,&s)==U_NONE&&n.mode==U_INFO); // never a restart command
    n=(unifi_nav_t){.page=U_DEVICES,.mode=U_MENU,.menu=1};
    assert(unifi_navigate(&n,U_OK,&s)==U_NONE&&n.rates&&n.mode==U_DETAIL);
    n=(unifi_nav_t){.page=U_VPN,.mode=U_DETAIL,.selection=2};unifi_navigate(&n,U_DOWN,&s);assert(n.selection==0);
    n=(unifi_nav_t){.page=U_SITES,.mode=U_DETAIL,.selection=1};assert(unifi_navigate(&n,U_OK,&s)==U_SELECT_SITE);
    n=(unifi_nav_t){.page=U_SETTINGS,.mode=U_MENU,.menu=2};
    assert(unifi_navigate(&n,U_OK,&s)==U_NONE&&n.mode==U_INFO);
    render(&n,&s,&c);assert(saw_diagnostics&&!saw_reconnect);
    s.demo=false;s.refreshing=true;s.http_status=0;
    render(&n,&s,&c);assert(!strcmp(api_state,"正在查询"));
    s.refreshing=false;strcpy(s.error,"Sites: transport failed");
    render(&n,&s,&c);assert(!strcmp(api_state,"查询未完成"));
    s.selected_site=UINT8_MAX;
    render(&n,&s,&c);assert(!strcmp(header_text,"AI Passport"));
    unifi_fixture(&s,&c);
    n=(unifi_nav_t){.page=U_SDWAN,.mode=U_INFO};
    render(&n,&s,&c);assert(saw_reconnect&&!saw_diagnostics);
    s.demo=false;
    n=(unifi_nav_t){.page=U_SDWAN};render(&n,&s,&c);assert(saw_mesh);
    n=(unifi_nav_t){.page=U_INTERNET};render(&n,&s,&c);assert(saw_current);
    n=(unifi_nav_t){.page=U_OVERVIEW,.mode=U_DETAIL};render(&n,&s,&c);assert(saw_unavailable_traffic);
    unifi_fixture(&s,&c);
    unifi_alerts_t alerts={0};unifi_observe(&alerts,&s,&c,1794067260);assert(alerts.count==0);
    c.wifi_connected=false;unifi_observe(&alerts,&s,&c,1794067261);assert(alerts.count==1);
    unifi_observe(&alerts,&s,&c,1794067262);assert(alerts.count==1);
    char b[40];unifi_duration(b,sizeof b,-1);assert(!strcmp(b,"未知"));unifi_rate(b,sizeof b,NAN,false);assert(!strcmp(b,"--"));
    for(int empty=0;empty<2;empty++) {
        if(empty){memset(&s,0,sizeof s);memset(&c,0,sizeof c);}
        for(int p=0;p<U_PAGE_COUNT;p++)for(int mode=U_HOME;mode<=U_INFO;mode++)for(int selection=0;selection<7;selection++) {
            n=(unifi_nav_t){.page=p,.mode=mode,.selection=selection};
            if(p==U_SITES&&selection>=s.site_count)n.selection=0;
            render(&n,&s,&c);if(p==U_DEVICES){n.rates=true;render(&n,&s,&c);}if(p==U_OVERVIEW){n.wan_details=true;render(&n,&s,&c);}
        }
    }
    printf("UniFi navigation, formats, alerts, %d layout states: PASS\n",frame);
}
