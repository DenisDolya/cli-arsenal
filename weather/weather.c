// weather.c
// ASCII terminal weather client in plain C using libcurl
// Build: gcc -O2 -Wall -o weather weather.c -lcurl -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__) || defined(__ANDROID__)
  #include <sys/ioctl.h>
  #include <termios.h>
#endif

/* ------------------------------------------------------------------
   HTTP helper
   ------------------------------------------------------------------ */
struct mem { char *ptr; size_t len; };
static size_t write_cb(void *data, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct mem *mem = (struct mem *)userp;
    char *ptr = realloc(mem->ptr, mem->len + realsize + 1);
    if(!ptr) return 0;
    mem->ptr = ptr;
    memcpy(mem->ptr + mem->len, data, realsize);
    mem->len += realsize;
    mem->ptr[mem->len] = 0;
    return realsize;
}
char *http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if(!curl) return NULL;
    struct mem chunk; chunk.ptr = malloc(1); chunk.len = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "weather-cli/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "[error] curl failed: %s\n", curl_easy_strerror(res));
        free(chunk.ptr); chunk.ptr = NULL;
    }
    curl_easy_cleanup(curl);
    return chunk.ptr; // caller frees
}

/* ------------------------------------------------------------------
   JSON tiny helpers (naive, for predictable Open-Meteo/ip-api payloads)
   ------------------------------------------------------------------ */

char *find_quoted_key_in_base(const char *base, const char *key) {
    char pat[256];
    if (snprintf(pat, sizeof(pat), "\"%s\"", key) >= (int)sizeof(pat)) return NULL;
    char *p = strstr(base, pat);
    while(p) {
        char *q = p + strlen(pat);
        while(*q && isspace((unsigned char)*q)) q++;
        if(*q == ':') return q + 1;
        p = strstr(p + 1, pat);
    }
    return NULL;
}

char *find_object_start(const char *json, const char *objkey) {
    char pat[256];
    if (snprintf(pat, sizeof(pat), "\"%s\"", objkey) >= (int)sizeof(pat)) return NULL;
    const char *p = strstr(json, pat);
    while(p) {
        const char *q = p + strlen(pat);
        while(*q && isspace((unsigned char)*q)) q++;
        if(*q == ':') {
            const char *r = q + 1;
            while(*r && isspace((unsigned char)*r)) r++;
            if(*r == '{') return (char *)r;
        }
        p = strstr(p + 1, pat);
    }
    return NULL;
}

int extract_double(const char *json, const char *key, double *out) {
    char *v = find_quoted_key_in_base(json, key);
    if(!v) return 0;
    while(*v && isspace((unsigned char)*v)) v++;
    char buf[64]; int bi = 0;
    while(*v && (isdigit((unsigned char)*v) || *v=='-' || *v=='+' || *v=='.' || *v=='e' || *v=='E')) {
        if(bi < (int)sizeof(buf)-1) buf[bi++] = *v;
        v++;
    }
    buf[bi] = 0;
    if(bi==0) return 0;
    *out = strtod(buf, NULL);
    return 1;
}

int extract_string_array_from_base(const char *base, const char *key, char ***arr) {
    char *v = find_quoted_key_in_base(base, key);
    if(!v) return 0;
    while(*v && *v != '[') v++;
    if(*v != '[') return 0;
    v++;
    int cap = 8, cnt = 0; char **res = malloc(cap * sizeof(char*));
    while(*v) {
        while(*v && isspace((unsigned char)*v)) v++;
        if(*v == ']') break;
        if(*v == '"') {
            v++;
            const char *s = v;
            while(*v && *v != '"') {
                if(*v == '\\' && *(v+1)) v += 2; else v++;
            }
            size_t len = v - s;
            char *out = malloc(len + 1); strncpy(out, s, len); out[len]=0;
            if(cnt >= cap) { cap *= 2; res = realloc(res, cap * sizeof(char*)); }
            res[cnt++] = out;
            if(*v == '"') v++;
            while(*v && *v != ',' && *v != ']') v++;
            if(*v == ',') v++;
        } else {
            while(*v && *v != ',' && *v != ']') v++;
            if(*v == ',') v++;
        }
    }
    *arr = res; return cnt;
}

int extract_double_array_from_base(const char *base, const char *key, double **out) {
    char *v = find_quoted_key_in_base(base, key);
    if(!v) return 0;
    while(*v && *v != '[') v++;
    if(*v != '[') return 0;
    v++;
    int cap = 16, cnt = 0; double *res = malloc(cap * sizeof(double));
    while(*v) {
        while(*v && isspace((unsigned char)*v)) v++;
        if(*v == ']') break;
        if(strncmp(v, "null", 4) == 0) {
            if(cnt >= cap) { cap *= 2; res = realloc(res, cap * sizeof(double)); }
            res[cnt++] = NAN; v += 4;
        } else {
            char nb[64]; int ni=0;
            while(*v && (isdigit((unsigned char)*v) || *v=='-'||*v=='+'||*v=='.'||*v=='e'||*v=='E')) {
                if(ni < (int)sizeof(nb)-1) nb[ni++] = *v;
                v++;
            }
            nb[ni]=0;
            if(ni>0) {
                double val = strtod(nb, NULL);
                if(cnt >= cap) { cap *= 2; res = realloc(res, cap * sizeof(double)); }
                res[cnt++] = val;
            } else {
                while(*v && *v != ',' && *v != ']') v++;
            }
        }
        while(*v && *v != ',' && *v != ']') v++;
        if(*v == ',') v++;
    }
    *out = res; return cnt;
}

/* ------------------------------------------------------------------
   weathercode mapping (IMPLEMENTED HERE to avoid implicit decl/linker error)
   ------------------------------------------------------------------ */

const char *weathercode_to_str(int code) {
    if(code == 0) return "clear";
    if(code == 1 || code == 2) return "mainly_clear";
    if(code == 3) return "overcast";
    if(code == 45 || code == 48) return "fog";
    if(code >= 51 && code <= 57) return "drizzle";
    if(code >= 61 && code <= 67) return "rain";
    if(code >= 71 && code <= 77) return "snow";
    if(code >= 80 && code <= 82) return "rain_shower";
    if(code == 95 || code == 96 || code == 99) return "thunder";
    return "unknown";
}

/* ------------------------------------------------------------------
   ANSI stuff (same as before)
   ------------------------------------------------------------------ */
#define ANSI_RESET "\x1b[0m"
#define ANSI_BOLD  "\x1b[1m"
#define ANSI_DIM   "\x1b[2m"
#define ANSI_CYAN  "\x1b[36m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE  "\x1b[34m"
#define ANSI_GREEN "\x1b[32m"
#define ANSI_MAGENTA "\x1b[35m"

const char *wc_color(int code) {
    if(code == 0) return ANSI_GREEN;
    if(code == 1 || code == 2) return ANSI_CYAN;
    if(code == 3) return ANSI_DIM;
    if(code >= 45 && code <= 48) return ANSI_DIM;
    if(code >= 51 && code <= 67) return ANSI_BLUE;
    if(code >= 71 && code <= 77) return ANSI_CYAN;
    if(code >= 80 && code <= 82) return ANSI_BLUE;
    if(code == 95 || code == 96 || code == 99) return ANSI_MAGENTA;
    return ANSI_RESET;
}

/* ------------------------------------------------------------------
   simple terminal helpers + rendering (kept mostly same)
   ------------------------------------------------------------------ */

int get_terminal_width() {
    int cols = 0;
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__) || defined(__ANDROID__)
    struct winsize w;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) cols = (int)w.ws_col;
#endif
    if(cols == 0) {
        char *env = getenv("COLUMNS");
        if(env) cols = atoi(env);
    }
    if(cols <= 0) cols = 80;
    return cols;
}

void center_and_trunc(const char *s, int width, char *out_buf) {
    if(width <= 0) { if(width==0) return; out_buf[0]=0; return; }
    int slen = s ? (int)strlen(s) : 0;
    if(slen <= width) {
        int left = (width - slen) / 2;
        int i = 0;
        for(; i < left; i++) out_buf[i] = ' ';
        if(s) memcpy(out_buf + i, s, slen);
        i += slen;
        for(; i < width; i++) out_buf[i] = ' ';
        out_buf[width] = 0;
        return;
    }
    if(width <= 3) { for(int i=0;i<width;i++) out_buf[i]='.'; out_buf[width]=0; return; }
    int keep = width - 3;
    int left = (width - (keep + 3)) / 2;
    int i = 0;
    for(; i < left; i++) out_buf[i] = ' ';
    if(keep > 0 && s) memcpy(out_buf + i, s, keep);
    i += keep;
    out_buf[i++]='.'; out_buf[i++]='.'; out_buf[i++]='.';
    for(; i < width; i++) out_buf[i] = ' ';
    out_buf[width] = 0;
}

void print_border_top_cols_window(int cols, const int *colw, int start_col, int end_col) {
    for(int c=start_col;c<=end_col;c++){
        putchar('+');
        for(int i=0;i<colw[c];i++) putchar('-');
    }
    putchar('+'); putchar('\n');
}

void print_row_centered_cols_window(char **items, const int *colw, int start_col, int end_col) {
    char *tmp = NULL;
    for(int c=start_col;c<=end_col;c++){
        putchar('|');
        int w = colw[c];
        tmp = realloc(tmp, w+1);
        tmp[w] = 0;
        if(items[c]) center_and_trunc(items[c], w, tmp);
        else { for(int i=0;i<w;i++) tmp[i]=' '; tmp[w]=0; }
        fwrite(tmp,1,w,stdout);
    }
    putchar('|'); putchar('\n');
    if(tmp) free(tmp);
}

void print_row_strings_cols_window(char **items, const int *colw, int start_col, int end_col) {
    char *tmp = NULL;
    for(int c=start_col;c<=end_col;c++){
        putchar('|');
        int w = colw[c];
        tmp = realloc(tmp, w+1);
        tmp[w] = 0;
        int slen = items[c] ? (int)strlen(items[c]) : 0;
        if(slen <= w) {
            int start = 1; if(w < 2) start = 0;
            int i = 0;
            for(; i < start; i++) tmp[i] = ' ';
            if(items[c]) memcpy(tmp + i, items[c], slen);
            i += slen;
            for(; i < w; i++) tmp[i] = ' ';
        } else {
            center_and_trunc(items[c], w, tmp);
        }
        fwrite(tmp,1,w,stdout);
    }
    putchar('|'); putchar('\n');
    if(tmp) free(tmp);
}

void print_temp_bars_cols_window(double *values, const int *colw, int start_col, int end_col) {
    double vmin=1e9, vmax=-1e9;
    for(int i=start_col;i<=end_col;i++) {
        if(!isnan(values[i])) { if(values[i] < vmin) vmin = values[i]; if(values[i] > vmax) vmax = values[i]; }
    }
    if(vmin > vmax) { vmin = vmax = 0; }
    for(int c=start_col;c<=end_col;c++){
        putchar('|');
        int w = colw[c];
        int inner = w > 0 ? w : 1;
        if(isnan(values[c])) {
            for(int i=0;i<inner;i++) putchar(' ');
        } else {
            double v = values[c];
            int fill = 0;
            if(vmax != vmin) fill = (int)((v - vmin) / (vmax - vmin) * inner + 0.5);
            if(fill < 0) {
                fill = 0;
            }
            if(fill > inner) {
                fill = inner;
            }
            for(int i=0;i<fill;i++) putchar('#');
            for(int i=fill;i<inner;i++) putchar(' ');
        }
    }
    putchar('|'); putchar('\n');
}

/* ------------------------------------------------------------------
   raw mode helpers (linux/posix)
   ------------------------------------------------------------------ */
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__) || defined(__ANDROID__)
static struct termios orig_term;
static int term_saved = 0;
void enable_raw_mode() {
    if(term_saved) return;
    if(tcgetattr(STDIN_FILENO, &orig_term) == -1) return;
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    term_saved = 1;
}
void disable_raw_mode() {
    if(!term_saved) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
    term_saved = 0;
}
#else
void enable_raw_mode() {}
void disable_raw_mode() {}
#endif

/* ------------------------------------------------------------------
   windowed renderer (left/right scroll) - kept largely same as previous
   ------------------------------------------------------------------ */

void render_windowed_table(
    char **date_items, char **temp_items, char **desc_items,
    double *bars, const int *colw, int cols, int term_w
) {
    int start = 0;
    int end = cols - 1;
    long used = 1;
    end = start;
    while(end < cols) {
        long add = 1 + colw[end];
        if(used + add > term_w) break;
        used += add;
        end++;
    }
    if(end == start) end = start;
    else end = end - 1;

    enable_raw_mode();
    while(1) {
        printf("\x1b[2J\x1b[H");
        printf(ANSI_BOLD ANSI_CYAN "Weather (←/→ or a/d to scroll, q to quit)" ANSI_RESET "\n");

        print_border_top_cols_window(cols, colw, start, end);
        print_row_centered_cols_window(date_items, colw, start, end);
        print_border_top_cols_window(cols, colw, start, end);

        // colored temp row (left aligned inside cell to avoid ANSI-length issues)
        char *tmp = NULL;
        for(int c=start;c<=end;c++) {
            putchar('|');
            int w = colw[c];
            tmp = realloc(tmp, w+1);
            tmp[w] = 0;
            const char *plain = temp_items[c];
            int slen = plain ? (int)strlen(plain) : 0;
            if(slen <= w) {
                int leading = 1; if(w < 2) leading = 0;
                int i=0; for(; i<leading; i++) tmp[i] = ' ';
                if(plain) memcpy(tmp+i, plain, slen);
                i += slen;
                for(; i<w; i++) tmp[i] = ' ';
                printf(ANSI_YELLOW);
                fwrite(tmp,1,w,stdout);
                printf(ANSI_RESET);
            } else {
                center_and_trunc(plain, w, tmp);
                fwrite(tmp,1,w,stdout);
            }
        }
        if(tmp) { free(tmp); tmp = NULL; }
        putchar('|'); putchar('\n');
        print_border_top_cols_window(cols, colw, start, end);

        print_temp_bars_cols_window(bars, colw, start, end);
        print_border_top_cols_window(cols, colw, start, end);

        // desc row with per-cell color
        for(int c=start;c<=end;c++) {
            putchar('|');
            int w = colw[c];
            const char *desc = desc_items[c];
            int code = -999;
            if(desc) {
                const char *p = strrchr(desc, '(');
                if(p) code = atoi(p+1);
            }
            const char *color = (code != -999) ? wc_color(code) : ANSI_RESET;
            char *cell = malloc(w+1);
            center_and_trunc(desc, w, cell);
            printf("%s", color);
            fwrite(cell,1,w,stdout);
            printf(ANSI_RESET);
            free(cell);
        }
        putchar('|'); putchar('\n');
        print_border_top_cols_window(cols, colw, start, end);

        printf("\n" ANSI_DIM "Legend: max/min temperatures (°C). Use arrows/a/d to scroll. q to quit." ANSI_RESET "\n");

        unsigned char seq[4];
        ssize_t r = read(STDIN_FILENO, seq, 3);
        if(r <= 0) continue;
        if(seq[0] == 'q' || seq[0] == 'Q') break;
        if(seq[0] == 'a' || seq[0] == 'A' ) {
            if(start > 0) start--;
            long used2 = 1; int e = start;
            while(e < cols) { long add = 1 + colw[e]; if(used2 + add > term_w) break; used2 += add; e++; }
            if(e == start) e = start; else e = e-1;
            end = e; continue;
        }
        if(seq[0] == 'd' || seq[0] == 'D') {
            if(end < cols-1) { start++; long used2 = 1; int e = start; while(e < cols) { long add = 1 + colw[e]; if(used2 + add > term_w) break; used2 += add; e++; } if(e == start) e = start; else e = e-1; end = e; }
            continue;
        }
        if(r == 3 && seq[0] == 0x1b && seq[1] == '[') {
            if(seq[2] == 'C') { if(end < cols-1) { start++; long used2 = 1; int e = start; while(e < cols) { long add = 1 + colw[e]; if(used2 + add > term_w) break; used2 += add; e++; } if(e == start) e = start; else e = e-1; end = e; } }
            else if(seq[2] == 'D') { if(start > 0) { start--; long used2 = 1; int e = start; while(e < cols) { long add = 1 + colw[e]; if(used2 + add > term_w) break; used2 += add; e++; } if(e == start) e = start; else e = e-1; end = e; } }
            continue;
        }
    }
    disable_raw_mode();
    printf("\x1b[2J\x1b[H");
}

/* ------------------------------------------------------------------
   main: glue (kept as previous but uses fixed helpers)
   ------------------------------------------------------------------ */

void usage(const char *p) { printf("Usage: %s [-days N] [-ip <IP>] [--yes-me]\n", p); }
char *trim_newline(char *s) { if(!s) return s; size_t L=strlen(s); while(L>0 && (s[L-1]=='\n' || s[L-1]=='\r')) { s[L-1]=0; L--; } return s; }

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const char *cli_ip = NULL;
    int days = 7;
    int auto_yes_me = 0;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i], "-ip")==0 && i+1<argc) cli_ip = argv[++i];
        else if(strcmp(argv[i], "-days")==0 && i+1<argc) { days = atoi(argv[++i]); if(days<1) days=1; if(days>14) days=14; }
        else if(strcmp(argv[i], "--yes-me")==0) auto_yes_me = 1;
        else if(strcmp(argv[i], "-h")==0 || strcmp(argv[i],"--help")==0) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 1; }
    }

    int interactive = isatty(fileno(stdin)) && isatty(fileno(stdout));
    char *chosen_ip = NULL;
    if(interactive && !auto_yes_me) {
        char buf[256];
        while(1) {
            if(cli_ip) printf("[log] -ip hint detected: %s\n", cli_ip);
            printf("[log] Enter IP (or type 'me' to auto-detect public IP): ");
            if(!fgets(buf, sizeof(buf), stdin)) {
                if(cli_ip) { chosen_ip = strdup(cli_ip); printf("[log] No input (EOF). Using -ip: %s\n", chosen_ip); }
                else { chosen_ip = NULL; printf("[log] No input (EOF). Using 'me'\n"); }
                break;
            }
            trim_newline(buf); char *p = buf; while(*p && isspace((unsigned char)*p)) p++;
            if(*p == 0) { printf("[log] Empty input. Please type an IP or 'me'.\n"); continue; }
            if(strcmp(p, "me")==0) { chosen_ip = NULL; printf("[log] Using 'me' -> auto-detect\n"); break; }
            chosen_ip = strdup(p); printf("[log] Using provided: %s\n", chosen_ip); break;
        }
    } else {
        if(cli_ip) chosen_ip = strdup(cli_ip);
        else chosen_ip = NULL;
    }

    char ip_api_url[512];
    if(chosen_ip) snprintf(ip_api_url, sizeof(ip_api_url), "http://ip-api.com/json/%s?fields=status,message,lat,lon,city,country,query", chosen_ip);
    else snprintf(ip_api_url, sizeof(ip_api_url), "http://ip-api.com/json/?fields=status,message,lat,lon,city,country,query");
    printf("[log] Fetching geolocation from: %s\n", ip_api_url);
    char *ip_json = http_get(ip_api_url);
    if(!ip_json) { fprintf(stderr,"[error] Failed to get IP geolocation.\n"); if(chosen_ip) free(chosen_ip); curl_global_cleanup(); return 1; }
    double lat=0, lon=0;
    if(!extract_double(ip_json, "lat", &lat) || !extract_double(ip_json, "lon", &lon)) {
        fprintf(stderr,"[error] Could not parse lat/lon.\n[debug] ip-api snippet:\n%.512s\n", ip_json);
        free(ip_json); if(chosen_ip) free(chosen_ip); curl_global_cleanup(); return 1;
    }
    char *city = NULL, *country = NULL, *queryip = NULL;
    {
        char *p = find_quoted_key_in_base(ip_json, "city");
        if(p) { while(*p && isspace((unsigned char)*p)) p++; if(*p=='"'){ p++; const char *s=p; while(*p && *p!='"'){ if(*p=='\\' && *(p+1)) p+=2; else p++; } size_t ln = p - s; city = malloc(ln+1); strncpy(city, s, ln); city[ln]=0; } }
        p = find_quoted_key_in_base(ip_json, "country");
        if(p) { while(*p && isspace((unsigned char)*p)) p++; if(*p=='"'){ p++; const char *s=p; while(*p && *p!='"'){ if(*p=='\\' && *(p+1)) p+=2; else p++; } size_t ln = p - s; country = malloc(ln+1); strncpy(country, s, ln); country[ln]=0; } }
        p = find_quoted_key_in_base(ip_json, "query");
        if(p) { while(*p && isspace((unsigned char)*p)) p++; if(*p=='"'){ p++; const char *s=p; while(*p && *p!='"'){ p++; } size_t ln = p - s; queryip = malloc(ln+1); strncpy(queryip, s, ln); queryip[ln]=0; } }
    }
    printf("[log] Geolocation: lat=%.6f lon=%.6f   city=%s country=%s query=%s\n", lat, lon, city?city:"n/a", country?country:"n/a", queryip?queryip:"n/a");
    free(ip_json);

    char weather_url[1024];
    snprintf(weather_url, sizeof(weather_url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&daily=temperature_2m_max,temperature_2m_min,weathercode&timezone=UTC&forecast_days=%d",
             lat, lon, days);
    printf("[log] Fetching weather from Open-Meteo: %s\n", weather_url);
    char *weather_json = http_get(weather_url);
    if(!weather_json) { fprintf(stderr,"[error] Failed to fetch weather data.\n"); free(city); free(country); free(queryip); if(chosen_ip) free(chosen_ip); curl_global_cleanup(); return 1; }

    char *daily_start = find_object_start(weather_json, "daily");
    const char *base = daily_start ? daily_start : weather_json;
    char **times = NULL; int tcount = extract_string_array_from_base(base, "time", &times);
    double *tmax = NULL; int maxcount = extract_double_array_from_base(base, "temperature_2m_max", &tmax);
    double *tmin = NULL; int mincount = extract_double_array_from_base(base, "temperature_2m_min", &tmin);
    double *wcode_d = NULL; int wcount = extract_double_array_from_base(base, "weathercode", &wcode_d);
    if(tcount == 0 || maxcount == 0 || mincount == 0) {
        fprintf(stderr,"[error] Weather response missing expected fields.\n[debug] weather snippet:\n%.2048s\n", weather_json);
        free(weather_json); free(city); free(country); free(queryip); if(chosen_ip) free(chosen_ip); curl_global_cleanup(); return 1;
    }
    int days_avail = tcount; if(maxcount < days_avail) days_avail = maxcount; if(mincount < days_avail) days_avail = mincount; if(wcount < days_avail) days_avail = wcount;
    if(days_avail <= 0) { fprintf(stderr,"[error] No entries.\n"); free(weather_json); free(city); free(country); free(queryip); if(chosen_ip) free(chosen_ip); curl_global_cleanup(); return 1; }

    int cols = days_avail;
    char **date_items = malloc(cols * sizeof(char*));
    char **temp_items = malloc(cols * sizeof(char*));
    char **desc_items = malloc(cols * sizeof(char*));
    double *bars = malloc(cols * sizeof(double));
    for(int i=0;i<cols;i++){
        char date_buf[64]={0}; if(times[i]) strncpy(date_buf, times[i], 10); else strncpy(date_buf,"n/a",sizeof(date_buf)-1);
        date_items[i] = strdup(date_buf);
        char temp_buf[128]={0}; if(i<maxcount && i<mincount && !isnan(tmax[i]) && !isnan(tmin[i])) snprintf(temp_buf,sizeof(temp_buf),"max %.0fC / min %.0fC", tmax[i], tmin[i]); else snprintf(temp_buf,sizeof(temp_buf),"no data");
        temp_items[i] = strdup(temp_buf);
        char desc_buf[128]={0};
        if(i<wcount && !isnan(wcode_d[i])) {
            int code=(int)wcode_d[i];
            snprintf(desc_buf, sizeof(desc_buf), "%s (%d)", weathercode_to_str(code), code);
        } else snprintf(desc_buf, sizeof(desc_buf),"n/a");
        desc_items[i] = strdup(desc_buf);
        if(i<maxcount) bars[i] = tmax[i]; else bars[i] = NAN;
    }

    int *colw = malloc(cols * sizeof(int));
    int term_w = get_terminal_width();
    for(int i=0;i<cols;i++){
        int m=0; int l;
        l=strlen(date_items[i]); if(l>m) m=l;
        l=strlen(temp_items[i]); if(l>m) m=l;
        l=strlen(desc_items[i]); if(l>m) m=l;
        int desired = m + 2; if(desired < 8) desired = 8;
        colw[i] = desired;
    }
    long total = 1; for(int i=0;i<cols;i++) total += (1 + colw[i]);
    if(!interactive || total <= term_w) {
        // print full table (no scroll)
        printf("\n" ANSI_BOLD ANSI_CYAN "ASCII Weather — location: " ANSI_RESET);
        if(city) printf("%s, ", city);
        if(country) printf("%s", country?country:"");
        if(queryip) printf(" (IP: %s)", queryip);
        printf("\nCoordinates: %.4f, %.4f — showing %d day(s)\n\n", lat, lon, cols);
        // top border
        for(int c=0;c<cols;c++){ putchar('+'); for(int i=0;i<colw[c];i++) putchar('-'); } putchar('+'); putchar('\n');
        // date row
        char *tmp = NULL;
        for(int c=0;c<cols;c++){ putchar('|'); int w=colw[c]; tmp = realloc(tmp, w+1); tmp[w]=0; center_and_trunc(date_items[c], w, tmp); fwrite(tmp,1,w,stdout); } putchar('|'); putchar('\n');
        for(int c=0;c<cols;c++){ putchar('+'); for(int i=0;i<colw[c];i++) putchar('-'); } putchar('+'); putchar('\n');
        // temp row colored
        for(int c=0;c<cols;c++){ putchar('|'); int w=colw[c]; char *cell = malloc(w+1); center_and_trunc(temp_items[c], w, cell); printf(ANSI_YELLOW); fwrite(cell,1,w,stdout); printf(ANSI_RESET); free(cell); } putchar('|'); putchar('\n');
        for(int c=0;c<cols;c++){ putchar('+'); for(int i=0;i<colw[c];i++) putchar('-'); } putchar('+'); putchar('\n');
        // bars
        double vmin=1e9, vmax=-1e9; for(int i=0;i<cols;i++){ if(!isnan(bars[i])){ if(bars[i]<vmin) vmin=bars[i]; if(bars[i]>vmax) vmax=bars[i]; } }
        if(vmin>vmax) vmin=vmax=0;
        for(int c=0;c<cols;c++){ putchar('|'); int w=colw[c]; if(isnan(bars[c])){ for(int i=0;i<w;i++) putchar(' '); } else { int fill=0; if(vmax!=vmin) fill=(int)((bars[c]-vmin)/(vmax-vmin)*w+0.5); if(fill<0) fill=0; if(fill>w) fill=w; for(int i=0;i<fill;i++) putchar('#'); for(int i=fill;i<w;i++) putchar(' '); } } putchar('|'); putchar('\n');
        for(int c=0;c<cols;c++){ putchar('+'); for(int i=0;i<colw[c];i++) putchar('-'); } putchar('+'); putchar('\n');
        // desc colored
        for(int c=0;c<cols;c++){ putchar('|'); int w=colw[c]; char *cell = malloc(w+1); center_and_trunc(desc_items[c], w, cell); int code=0; const char *p = strrchr(desc_items[c], '('); if(p) code = atoi(p+1); const char *color = wc_color(code); printf("%s", color); fwrite(cell,1,w,stdout); printf(ANSI_RESET); free(cell); } putchar('|'); putchar('\n');
        for(int c=0;c<cols;c++){ putchar('+'); for(int i=0;i<colw[c];i++) putchar('-'); } putchar('+'); putchar('\n');
        printf("\n" ANSI_DIM "Legend: max/min temperatures (°C). Bar shows relative max temps across shown days." ANSI_RESET "\n");
    } else {
        render_windowed_table(date_items, temp_items, desc_items, bars, colw, cols, term_w);
        printf(ANSI_BOLD ANSI_CYAN "ASCII Weather — " ANSI_RESET);
        if(city) printf("%s, ", city);
        if(country) printf("%s", country?country:"");
        if(queryip) printf(" (IP: %s)", queryip);
        printf("\n\n");
    }

    // cleanup
    for(int i=0;i<cols;i++){ free(date_items[i]); free(temp_items[i]); free(desc_items[i]); free(times[i]); }
    free(date_items); free(temp_items); free(desc_items); free(times);
    free(colw); free(bars); free(tmax); free(tmin); free(wcode_d);
    free(city); free(country); free(queryip);
    free(weather_json);
    if(chosen_ip) free(chosen_ip);
    curl_global_cleanup();
    return 0;
}