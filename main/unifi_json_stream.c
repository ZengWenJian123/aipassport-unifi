#include "unifi_json_stream.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { O_FIRST, O_KEY, O_COLON, O_VALUE, O_AFTER, A_FIRST, A_VALUE, A_AFTER };
static bool fail(ujson_stream_t *s) { s->failed = true; return false; }
static bool append(ujson_stream_t *s, unsigned char c) {
    if (s->used + 1 >= sizeof(s->token)) return fail(s);
    s->token[s->used++] = (char)c; s->token[s->used] = 0; return true;
}
static bool utf8(ujson_stream_t *s, uint32_t u) {
    if (u == 0) return fail(s); /* C strings cannot represent embedded NUL. */
    if (u < 0x80) return append(s, u);
    if (u < 0x800) return append(s, 0xc0 | (u >> 6)) && append(s, 0x80 | (u & 63));
    if (u < 0x10000) return append(s, 0xe0 | (u >> 12)) && append(s, 0x80 | ((u >> 6) & 63)) && append(s, 0x80 | (u & 63));
    return append(s, 0xf0 | (u >> 18)) && append(s, 0x80 | ((u >> 12) & 63)) && append(s, 0x80 | ((u >> 6) & 63)) && append(s, 0x80 | (u & 63));
}
static bool path_add(ujson_stream_t *s, const char *part) {
    size_t n = strlen(s->path), m = strlen(part);
    if (n + m + 2 >= sizeof(s->path)) return fail(s);
    s->path[n] = '/'; memcpy(s->path + n + 1, part, m + 1); return true;
}
static bool make_path(ujson_stream_t *s, bool leaf) {
    s->path[0] = 0;
    for (unsigned i = 1; i < s->depth; i++) if (!path_add(s, s->frames[i].segment)) return false;
    if (leaf && s->depth) {
        ujson_frame_t *f = &s->frames[s->depth - 1]; char index[24];
        if (f->array) { snprintf(index, sizeof(index), "%u", f->index); return path_add(s, index); }
        return path_add(s, f->key);
    }
    return true;
}
static bool emit(ujson_stream_t *s, ujson_event_t ev, bool leaf) {
    return make_path(s, leaf) && (!s->callback || s->callback(s->user, ev, s->path, s->token)) ? true : fail(s);
}
static bool value_allowed(ujson_stream_t *s) {
    if (!s->depth) return !s->started;
    unsigned st = s->frames[s->depth-1].state;
    return st == O_VALUE || st == A_FIRST || st == A_VALUE;
}
static void consume(ujson_stream_t *s) {
    if (!s->depth) { s->started = true; return; }
    ujson_frame_t *f = &s->frames[s->depth-1];
    f->state = f->array ? A_AFTER : O_AFTER;
    if (f->array) f->index++;
}
static bool number_valid(const char *p) {
    if (*p == '-') p++;
    if (*p == '0') p++;
    else { if (*p < '1' || *p > '9') return false; while (isdigit((unsigned char)*p)) p++; }
    if (*p == '.') { p++; if (!isdigit((unsigned char)*p)) return false; while (isdigit((unsigned char)*p)) p++; }
    if (*p == 'e' || *p == 'E') { p++; if (*p == '+' || *p == '-') p++; if (!isdigit((unsigned char)*p)) return false; while (isdigit((unsigned char)*p)) p++; }
    return *p == 0;
}
static bool primitive(ujson_stream_t *s) {
    ujson_event_t ev;
    if (!strcmp(s->token, "null")) ev = UJSON_NULL;
    else if (!strcmp(s->token, "true") || !strcmp(s->token, "false")) ev = UJSON_BOOL;
    else if (number_valid(s->token)) ev = UJSON_NUMBER;
    else return fail(s);
    if (!emit(s, ev, true)) return false;
    consume(s); if (!s->depth) s->done = true; s->mode = 0; return true;
}
void ujson_init(ujson_stream_t *s, ujson_callback_t cb, void *user) { memset(s, 0, sizeof(*s)); s->callback = cb; s->user = user; }
bool ujson_feed(ujson_stream_t *s, const char *data, size_t n) {
    if (s->failed) return false;
    for (size_t i=0; i<n; i++) {
        unsigned char c = (unsigned char)data[i];
        if (s->mode == 1) {
            if (s->unicode_digits) {
                int h = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
                if (h < 0) return fail(s);
                s->unicode = (s->unicode << 4) | (unsigned)h;
                if (--s->unicode_digits == 0) {
                    uint32_t u=s->unicode;
                    if (u >= 0xd800 && u <= 0xdbff) { if (s->high_surrogate) return fail(s); s->high_surrogate=u; }
                    else if (u >= 0xdc00 && u <= 0xdfff) { if (!s->high_surrogate) return fail(s); u=0x10000+((s->high_surrogate-0xd800)<<10)+(u-0xdc00); s->high_surrogate=0; if (!utf8(s,u)) return false; }
                    else { if (s->high_surrogate || !utf8(s,u)) return fail(s); }
                }
                continue;
            }
            if (s->escape) {
                s->escape=false;
                if (c=='u') { s->unicode_digits=4; s->unicode=0; continue; }
                if (s->high_surrogate) return fail(s);
                const char *from="\"\\/bfnrt", *to="\"\\/\b\f\n\r\t"; const char *p=strchr(from,c);
                if (!p || !append(s,(unsigned char)to[p-from])) return fail(s);
                continue;
            }
            if (c=='\\') { s->escape=true; continue; }
            if (s->high_surrogate) return fail(s);
            if (c=='\"') {
                s->mode=0;
                if (s->key_token) {
                    /* JSON object keys may contain '/' (controller package
                     * versions do). Escape JSON Pointer segments so such keys
                     * cannot masquerade as nested paths used by the parser. */
                    ujson_frame_t *f=&s->frames[s->depth-1];size_t j=0;
                    for(size_t k=0;k<s->used;k++){
                        char c=s->token[k];size_t need=(c=='/'||c=='~')?2:1;
                        if(j+need>=sizeof(f->key))return fail(s);
                        if(need==2){f->key[j++]='~';f->key[j++]=c=='/'?'1':'0';}
                        else f->key[j++]=c;
                    }
                    f->key[j]=0;f->state=O_COLON;
                } else { if (!emit(s,UJSON_STRING,true)) return false; consume(s); if (!s->depth) s->done=true; }
            } else if (c < 0x20 || !append(s,c)) return fail(s);
            continue;
        }
        if (s->mode == 2) {
            if (c==',' || c=='}' || c==']' || isspace(c)) { if (!primitive(s)) return false; }
            else { if (!append(s,c)) return false; continue; }
        }
        if (isspace(c)) continue;
        if (s->done) return fail(s);
        ujson_frame_t *f=s->depth?&s->frames[s->depth-1]:NULL;
        if (c=='}' || c==']') {
            if (!f || (c==']') != f->array) return fail(s);
            if (!(f->state==O_FIRST || f->state==O_AFTER || f->state==A_FIRST || f->state==A_AFTER)) return fail(s);
            if (!emit(s,f->array?UJSON_END_ARRAY:UJSON_END_OBJECT,false)) return false;
            s->depth--; if (!s->depth) s->done=true; continue;
        }
        if (c==',') { if (!f || (f->state!=O_AFTER && f->state!=A_AFTER)) return fail(s); f->state=f->array?A_VALUE:O_KEY; continue; }
        if (c==':') { if (!f || f->state!=O_COLON) return fail(s); f->state=O_VALUE; continue; }
        bool key=f && !f->array && (f->state==O_FIRST || f->state==O_KEY);
        if (c=='\"') {
            if (!key && !value_allowed(s)) return fail(s);
            s->mode=1; s->key_token=key; s->used=0; s->token[0]=0; continue;
        }
        if (!value_allowed(s)) return fail(s);
        if (c=='{' || c=='[') {
            if (s->depth >= 20) return fail(s);
            char segment[64]="";
            if (f) { if(f->array) snprintf(segment,sizeof(segment),"%u",f->index); else memcpy(segment,f->key,sizeof(segment)); }
            consume(s); f=&s->frames[s->depth++]; memset(f,0,sizeof(*f)); memcpy(f->segment,segment,sizeof(segment)); f->array=c=='['; f->state=f->array?A_FIRST:O_FIRST;
            s->token[0]=0; if (!emit(s,f->array?UJSON_ARRAY:UJSON_OBJECT,false)) return false;
        } else {
            s->mode=2; s->used=0; s->token[0]=0; if (!append(s,c)) return false;
        }
    }
    return !s->failed;
}
bool ujson_finish(ujson_stream_t *s) { if(s->mode==2 && !primitive(s)) return false; return !s->failed && s->done && !s->depth && !s->mode; }
