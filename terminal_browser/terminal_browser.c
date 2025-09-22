/*
 terminal_browser.c
 Minimal terminal HTML viewer (C11)
 - libcurl for HTTP/HTTPS
 - supports local files and built-in "test"
 - ncurses for rendering
 - Transparent DIV/SECTION/ARTICLE/SPAN (they are not drawn as boxes)
 - Normal text: dim (A_DIM)
 - Headers: bold + white
 - Links: blue + underline
 - Controls: q=quit, r=reload, arrows/PgUp/PgDn scroll
 - ASCII-only sanitization (non-ASCII -> '?')
 
 Build:
   gcc -std=c11 terminal_browser.c -o browser -lcurl -lncurses
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <ctype.h>
#include <curl/curl.h>
#include <ncurses.h>
#include <locale.h>

/* --- strcasestr fallback --- */
static char *strcasestr_local(const char *haystack, const char *needle) {
    if(!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if(nlen == 0) return (char*)haystack;
    for(const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while(p[i] && i < nlen && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if(i == nlen) return (char*)p;
    }
    return NULL;
}
#define strcasestr strcasestr_local

/* ---------- Data structures ---------- */

typedef enum {
    NODE_TEXT,
    NODE_BR,
    NODE_HR,
    NODE_DIV, /* also used for unknown/transparent containers */
    NODE_HEADER, /* h1..h6 */
    NODE_PARAGRAPH,
    NODE_PRE,
    NODE_CODE,
    NODE_BOLD,
    NODE_ITALIC,
    NODE_MARK,
    NODE_UNDER,
    NODE_STRIKE,
    NODE_BLOCKQUOTE,
    NODE_UL, NODE_OL, NODE_LI,
    NODE_DL, NODE_DT, NODE_DD,
    NODE_IMG, NODE_FIGURE, NODE_FIGCAP,
    NODE_DETAILS, NODE_SUMMARY,
    NODE_TABLE, NODE_TR, NODE_TD, NODE_TH,
    NODE_A,
    NODE_FORM, NODE_INPUT, NODE_TEXTAREA, NODE_SELECT, NODE_BUTTON,
    NODE_MAIN, NODE_HEADERBAR, NODE_FOOTER
} NodeType;

typedef struct Attr {
    char *name;
    char *value;
    struct Attr *next;
} Attr;

typedef struct Node {
    NodeType type;
    char *text; /* owned text for text nodes */
    Attr *attrs;
    struct Node **children;
    int child_count;
    int child_cap;
    struct Node *parent;
    int expanded; /* details */
    int list_number; /* for ol indexing */
} Node;

/* ---------- helpers ---------- */
static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if(!p) { endwin(); fprintf(stderr, "Out of memory\n"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    if(!s) return NULL;
    char *r = strdup(s);
    if(!r) { endwin(); fprintf(stderr, "Out of memory\n"); exit(1); }
    return r;
}

/* Attr helpers */
static Attr *attr_create(const char *name, const char *value) {
    Attr *a = xmalloc(sizeof(Attr));
    a->name = xstrdup(name?name:"");
    a->value = xstrdup(value?value:"");
    a->next = NULL;
    return a;
}
static void attr_free_all(Attr *a) {
    while(a) {
        Attr *n = a->next;
        free(a->name);
        free(a->value);
        free(a);
        a = n;
    }
}
static const char *attr_get_value(Attr *a, const char *name) {
    for(Attr *p=a; p; p=p->next) {
        if(strcasecmp(p->name, name) == 0) return p->value;
    }
    return NULL;
}

/* Node helpers */
static Node *node_create(NodeType t) {
    Node *n = xmalloc(sizeof(Node));
    n->type = t;
    n->text = NULL;
    n->attrs = NULL;
    n->children = NULL;
    n->child_count = 0;
    n->child_cap = 0;
    n->parent = NULL;
    n->expanded = 0;
    n->list_number = 0;
    return n;
}
static void node_append_child(Node *parent, Node *child) {
    if(!parent || !child) return;
    if(parent->child_count + 1 > parent->child_cap) {
        parent->child_cap = parent->child_cap ? parent->child_cap * 2 : 8;
        parent->children = realloc(parent->children, sizeof(Node*) * parent->child_cap);
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}
static void node_free_recursive(Node *n) {
    if(!n) return;
    if(n->text) free(n->text);
    attr_free_all(n->attrs);
    for(int i=0;i<n->child_count;i++) node_free_recursive(n->children[i]);
    if(n->children) free(n->children);
    free(n);
}

/* parse attributes (simple) */
static Attr *parse_attrs(const char *s) {
    Attr *head = NULL, **tail = &head;
    if(!s) return NULL;
    const char *p = s;
    while(*p) {
        while(*p && isspace((unsigned char)*p)) p++;
        if(!*p) break;
        char name[128] = {0}; int ni=0;
        while(*p && (isalnum((unsigned char)*p) || *p=='-' || *p==':') && ni+1 < (int)sizeof(name)) name[ni++] = *p++;
        name[ni]=0;
        while(*p && isspace((unsigned char)*p)) p++;
        if(*p == '=') p++;
        while(*p && isspace((unsigned char)*p)) p++;
        char value[512] = {0}; int vi=0;
        if(*p == '"' || *p == '\'') {
            char q = *p++; while(*p && *p != q && vi+1 < (int)sizeof(value)) value[vi++] = *p++;
            value[vi]=0; if(*p == q) p++;
        } else {
            while(*p && !isspace((unsigned char)*p) && *p!='>' && vi+1 < (int)sizeof(value)) value[vi++] = *p++;
            value[vi]=0;
        }
        if(name[0]) {
            Attr *a = attr_create(name, value);
            *tail = a; tail = &a->next;
        } else break;
    }
    return head;
}

/* ---------- fetch (curl + file) ---------- */
struct Mem { char *buf; size_t size; };
static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsz = size * nmemb;
    struct Mem *m = (struct Mem*)userdata;
    char *n = realloc(m->buf, m->size + realsz + 1);
    if(!n) return 0;
    m->buf = n;
    memcpy(m->buf + m->size, ptr, realsz);
    m->size += realsz;
    m->buf[m->size] = '\0';
    return realsz;
}
static char *read_file_local(const char *path) {
    if(!path) return NULL;
    FILE *f = fopen(path, "rb");
    if(!f) return NULL;
    if(fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long s = ftell(f);
    if(s < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)s + 1);
    if(!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)s, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}
static char *fetch_url(const char *url) {
    if(!url) return NULL;
    /* special: "test" -> use builtin handled by caller */
    /* file:// */
    if(strncmp(url, "file://", 7) == 0) {
        return read_file_local(url + 7);
    }
    /* absolute or relative path (no scheme) */
    if(url[0] == '/' || (strlen(url) > 1 && url[1] == ':')) { /* unix abs or windows drive letter */
        return read_file_local(url);
    }
    /* http/https */
    if(strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        CURL *curl = curl_easy_init();
        if(!curl) return NULL;
        struct Mem m;
        m.buf = malloc(1);
        m.size = 0;
        m.buf[0] = '\0';
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "TermBrowser/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        CURLcode rc = curl_easy_perform(curl);
        if(rc != CURLE_OK) {
            free(m.buf);
            m.buf = NULL;
        }
        curl_easy_cleanup(curl);
        return m.buf;
    }
    /* otherwise try as local file relative path */
    return read_file_local(url);
}

/* ---------- sanitize & strip ---------- */
static void sanitize_ascii_inplace(char *buf) {
    if(!buf) return;
    for(size_t i=0; buf[i]; ++i) {
        unsigned char c = (unsigned char)buf[i];
        if(c >= 128) buf[i] = '?';
        else if(c < 32 && c != '\n' && c != '\t' && c != '\r') buf[i] = ' ';
    }
}
static char *strip_script_style_meta(const char *src) {
    if(!src) return NULL;
    size_t n = strlen(src);
    char *out = xmalloc(n+1);
    size_t o = 0;
    for(size_t i=0;i<n;i++) {
        if(src[i] == '<') {
            /* skip script */
            if(i+8 < n && (strncasecmp(src+i+1,"script",6)==0 || strncasecmp(src+i+1,"/script",7)==0)) {
                char *end = strcasestr(src+i, "</script>");
                if(end) { i = (end - src) + 8 - 1; continue; } else break;
            }
            /* skip style */
            if(i+7 < n && (strncasecmp(src+i+1,"style",5)==0 || strncasecmp(src+i+1,"/style",6)==0)) {
                char *end = strcasestr(src+i, "</style>");
                if(end) { i = (end - src) + 7 - 1; continue; } else break;
            }
            /* skip meta/link tags entirely */
            if(i+5 < n && (strncasecmp(src+i+1,"meta",4)==0 || strncasecmp(src+i+1,"link",4)==0)) {
                char *gt = strchr(src+i, '>');
                if(gt) { i = (gt - src); continue; }
            }
        }
        out[o++] = src[i];
    }
    out[o] = '\0';
    return out;
}

/* ---------- simple HTML parser (streaming, not full spec) ---------- */
static Node *parse_html_tree(const char *html) {
    size_t pos = 0;
    size_t len = strlen(html);
    Node *root = node_create(NODE_DIV);
    Node **stack = xmalloc(sizeof(Node*) * 4096);
    int sp = 0;
    stack[sp++] = root;

    while(pos < len) {
        if(html[pos] == '<') {
            size_t gt = pos+1;
            while(gt < len && html[gt] != '>') gt++;
            if(gt >= len) break;
            size_t ilen = gt - (pos+1);
            char *inside = xmalloc(ilen + 1);
            memcpy(inside, html + pos + 1, ilen);
            inside[ilen] = '\0';
            /* comment */
            if(strncmp(inside, "!--", 3) == 0) {
                char *end = strstr(html + pos + 4, "-->");
                if(end) pos = (end - html) + 3;
                free(inside);
                continue;
            }
            const char *p = inside;
            while(*p && isspace((unsigned char)*p)) p++;
            int closing = 0;
            if(*p == '/') { closing = 1; p++; }
            char tag[64]; size_t ti = 0;
            while(*p && (isalnum((unsigned char)*p) || *p == '-') && ti + 1 < sizeof(tag)) tag[ti++] = tolower((unsigned char)*p++);
            tag[ti] = '\0';
            char attrs_sub[512] = {0};
            if(*p) strncpy(attrs_sub, p, sizeof(attrs_sub)-1);

            if(closing) {
                /* pop stack until matching tag type */
                for(int i = sp-1; i > 0; --i) {
                    Node *n = stack[i];
                    int match = 0;
                    switch(n->type) {
                        case NODE_DIV: if(strcmp(tag,"div")==0) match=1; break;
                        case NODE_MAIN: if(strcmp(tag,"main")==0) match=1; break;
                        case NODE_HEADERBAR: if(strcmp(tag,"header")==0) match=1; break;
                        case NODE_FOOTER: if(strcmp(tag,"footer")==0) match=1; break;
                        case NODE_HEADER: if(tag[0]=='h') match=1; break;
                        case NODE_PARAGRAPH: if(strcmp(tag,"p")==0) match=1; break;
                        case NODE_PRE: if(strcmp(tag,"pre")==0) match=1; break;
                        case NODE_CODE: if(strcmp(tag,"code")==0) match=1; break;
                        case NODE_BLOCKQUOTE: if(strcmp(tag,"blockquote")==0 || strcmp(tag,"q")==0) match=1; break;
                        case NODE_UL: if(strcmp(tag,"ul")==0) match=1; break;
                        case NODE_OL: if(strcmp(tag,"ol")==0) match=1; break;
                        case NODE_LI: if(strcmp(tag,"li")==0) match=1; break;
                        case NODE_TABLE: if(strcmp(tag,"table")==0) match=1; break;
                        case NODE_TR: if(strcmp(tag,"tr")==0) match=1; break;
                        case NODE_TD: if(strcmp(tag,"td")==0) match=1; break;
                        case NODE_TH: if(strcmp(tag,"th")==0) match=1; break;
                        case NODE_A: if(strcmp(tag,"a")==0) match=1; break;
                        case NODE_DETAILS: if(strcmp(tag,"details")==0) match=1; break;
                        case NODE_SUMMARY: if(strcmp(tag,"summary")==0) match=1; break;
                        case NODE_FIGURE: if(strcmp(tag,"figure")==0) match=1; break;
                        case NODE_FIGCAP: if(strcmp(tag,"figcaption")==0) match=1; break;
                        default: break;
                    }
                    if(match) { sp = i; break; }
                }
            } else {
                Node *node = NULL;
                if(strcmp(tag,"br")==0) { node = node_create(NODE_BR); node_append_child(stack[sp-1], node); }
                else if(strcmp(tag,"hr")==0) { node = node_create(NODE_HR); node_append_child(stack[sp-1], node); }
                else if(strcmp(tag,"div")==0) { node = node_create(NODE_DIV); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"main")==0) { node = node_create(NODE_MAIN); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(tag[0]=='h' && isdigit((unsigned char)tag[1])) { node = node_create(NODE_HEADER); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"p")==0) { node = node_create(NODE_PARAGRAPH); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"pre")==0) { node = node_create(NODE_PRE); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"code")==0) { node = node_create(NODE_CODE); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"blockquote")==0 || strcmp(tag,"q")==0) { node = node_create(NODE_BLOCKQUOTE); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"ul")==0) { node = node_create(NODE_UL); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"ol")==0) { node = node_create(NODE_OL); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"li")==0) { node = node_create(NODE_LI); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"dl")==0) { node = node_create(NODE_DL); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"dt")==0) { node = node_create(NODE_DT); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"dd")==0) { node = node_create(NODE_DD); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"figure")==0) { node = node_create(NODE_FIGURE); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"figcaption")==0) { node = node_create(NODE_FIGCAP); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"details")==0) { node = node_create(NODE_DETAILS); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"summary")==0) { node = node_create(NODE_SUMMARY); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"table")==0) { node = node_create(NODE_TABLE); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"tr")==0) { node = node_create(NODE_TR); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"td")==0) { node = node_create(NODE_TD); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"th")==0) { node = node_create(NODE_TH); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"a")==0) { node = node_create(NODE_A); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"img")==0) { node = node_create(NODE_IMG); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); /* self-close */ }
                else if(strcmp(tag,"form")==0) { node = node_create(NODE_FORM); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"input")==0) { node = node_create(NODE_INPUT); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); }
                else if(strcmp(tag,"textarea")==0) { node = node_create(NODE_TEXTAREA); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"select")==0) { node = node_create(NODE_SELECT); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"button")==0) { node = node_create(NODE_BUTTON); node->attrs = parse_attrs(attrs_sub); node_append_child(stack[sp-1], node); }
                else if(strcmp(tag,"header")==0) { node = node_create(NODE_HEADERBAR); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"footer")==0) { node = node_create(NODE_FOOTER); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"strong")==0 || strcmp(tag,"b")==0) { node = node_create(NODE_BOLD); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"em")==0 || strcmp(tag,"i")==0 || strcmp(tag,"cite")==0 || strcmp(tag,"dfn")==0 || strcmp(tag,"address")==0) { node = node_create(NODE_ITALIC); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"mark")==0) { node = node_create(NODE_MARK); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"u")==0 || strcmp(tag,"ins")==0 || strcmp(tag,"abbr")==0) { node = node_create(NODE_UNDER); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"del")==0) { node = node_create(NODE_STRIKE); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else if(strcmp(tag,"samp")==0) { node = node_create(NODE_CODE); node_append_child(stack[sp-1], node); stack[sp++]=node; }
                else {
                    /* unknown -> transparent DIV container */
                    node = node_create(NODE_DIV);
                    node->attrs = parse_attrs(attrs_sub);
                    node_append_child(stack[sp-1], node);
                    stack[sp++] = node;
                }
            }
            free(inside);
            pos = gt + 1;
        } else {
            /* text */
            size_t start = pos;
            while(pos < len && html[pos] != '<') pos++;
            size_t chunklen = pos - start;
            if(chunklen > 0) {
                char *chunk = xstrdup(html + start);
                chunk[chunklen] = '\0';
                Node *parent = stack[sp-1];
                if(parent->type == NODE_PRE || parent->type == NODE_CODE) {
                    Node *tn = node_create(NODE_TEXT);
                    tn->text = chunk;
                    node_append_child(parent, tn);
                } else {
                    /* collapse whitespace */
                    char *out = xmalloc(chunklen + 1);
                    size_t oi = 0; int last_space = 0;
                    for(size_t i=0;i<chunklen;i++) {
                        char ch = chunk[i];
                        if(ch == '\r') continue;
                        if(ch == '\n' || ch == '\t') ch = ' ';
                        if(isspace((unsigned char)ch)) {
                            if(!last_space) { out[oi++] = ' '; last_space = 1; }
                        } else { out[oi++] = ch; last_space = 0; }
                    }
                    while(oi > 0 && out[oi-1] == ' ') oi--;
                    out[oi] = '\0';
                    if(oi > 0) {
                        Node *tn = node_create(NODE_TEXT);
                        tn->text = out;
                        node_append_child(parent, tn);
                    } else free(out);
                    free(chunk);
                }
            }
        }
    }

    free(stack);
    return root;
}

/* ---------- rendering ---------- */

#define PAD_LINES 20000
static WINDOW *g_pad = NULL;
static int g_pad_h = PAD_LINES;
static int g_pad_w = 0;
static int g_term_h = 0, g_term_w = 0;
static int g_pad_cur_y = 0;

/* Transparent tags: don't draw box/extra lines for them */
static int is_transparent(NodeType t) {
    return (t == NODE_DIV || t == NODE_MAIN || t == NODE_HEADERBAR || t == NODE_FOOTER);
}

/* wrapped text renderer */
static void render_wrapped_text(int *y, int indent, const char *txt, int is_dim, int is_bold, int color_pair) {
    if(!txt) return;
    int curx = indent;
    size_t len = strlen(txt);
    char word[1024]; size_t wi = 0;
    for(size_t i=0;i<=len;i++) {
        char c = txt[i];
        if(c == ' ' || c == '\0') {
            if(wi > 0) {
                word[wi] = '\0';
                if(curx + (int)wi >= g_term_w) { (*y)++; curx = indent; }
                if(color_pair) wattron(g_pad, COLOR_PAIR(color_pair));
                if(is_bold) wattron(g_pad, A_BOLD);
                if(is_dim) wattron(g_pad, A_DIM);
                mvwaddnstr(g_pad, *y, curx, word, (int)wi);
                if(is_dim) wattroff(g_pad, A_DIM);
                if(is_bold) wattroff(g_pad, A_BOLD);
                if(color_pair) wattroff(g_pad, COLOR_PAIR(color_pair));
                curx += wi;
                if(c == ' ') {
                    if(curx + 1 >= g_term_w) { (*y)++; curx = indent; }
                    else { mvwaddch(g_pad, *y, curx, ' '); curx++; }
                }
                wi = 0;
            } else {
                if(c == ' ') {
                    if(curx + 1 >= g_term_w) { (*y)++; curx = indent; }
                    else { mvwaddch(g_pad, *y, curx, ' '); curx++; }
                }
            }
        } else {
            if(wi + 1 < sizeof(word)) word[wi++] = c;
        }
    }
    (*y)++;
}

/* code/pre block box (we keep box for pre/code) */
static void render_code_block(int *y, const char *text) {
    int boxw = g_term_w - 4; if(boxw < 10) boxw = 10;
    mvwprintw(g_pad, *y, 0, "+");
    for(int i=0;i<boxw;i++) waddch(g_pad, '-');
    waddch(g_pad, '+'); (*y)++;
    if(text) {
        char *dup = xstrdup(text);
        char *ln = strtok(dup, "\n");
        while(ln) {
            mvwprintw(g_pad, *y, 0, "| ");
            int len = (int)strlen(ln);
            if(len > boxw) {
                char tmp[1024]; strncpy(tmp, ln, boxw); tmp[boxw]=0;
                mvwprintw(g_pad, *y, 2, "%s", tmp);
            } else {
                mvwprintw(g_pad, *y, 2, "%s", ln);
                for(int sp=0; sp<boxw - len; sp++) waddch(g_pad, ' ');
            }
            waddch(g_pad, ' '); waddch(g_pad, '|'); (*y)++;
            ln = strtok(NULL, "\n");
        }
        free(dup);
    }
    mvwprintw(g_pad, *y, 0, "+");
    for(int i=0;i<boxw;i++) waddch(g_pad, '-');
    waddch(g_pad, '+'); (*y)++;
}

/* simple table renderer */
static void render_table_node(int *y, Node *table) {
    if(!table) return;
    int rows = 0, cols = 0;
    char ***cells = NULL;
    for(int i=0;i<table->child_count;i++) {
        Node *r = table->children[i];
        if(r->type != NODE_TR) continue;
        int ccount = 0;
        for(int j=0;j<r->child_count;j++) if(r->children[j]->type == NODE_TD || r->children[j]->type == NODE_TH) ccount++;
        if(ccount == 0) continue;
        cells = realloc(cells, sizeof(char**)*(rows+1));
        cells[rows] = calloc(ccount+1, sizeof(char*));
        int ci = 0;
        for(int j=0;j<r->child_count;j++) {
            Node *cnode = r->children[j];
            if(cnode->type != NODE_TD && cnode->type != NODE_TH) continue;
            size_t cap = 256; char *buf = malloc(cap); buf[0]=0; size_t bl=0;
            for(int k=0;k<cnode->child_count;k++) {
                Node *t = cnode->children[k];
                if(t->type == NODE_TEXT && t->text) {
                    size_t need = strlen(t->text);
                    if(bl + need + 2 > cap) { cap = (bl + need + 2)*2; buf = realloc(buf, cap); }
                    if(bl) buf[bl++]=' ';
                    strcpy(buf+bl, t->text); bl += need;
                }
            }
            cells[rows][ci++] = buf;
        }
        if(ci > cols) cols = ci;
        rows++;
    }
    if(rows == 0) return;
    int *colw = calloc(cols, sizeof(int));
    for(int c=0;c<cols;c++) {
        int mw = 1;
        for(int r=0;r<rows;r++) {
            if(cells[r] && cells[r][c]) {
                int L = (int)strlen(cells[r][c]);
                if(L > mw) mw = L;
            }
        }
        colw[c] = mw;
    }
    mvwaddch(g_pad, *y, 0, '+'); int curx=1;
    for(int c=0;c<cols;c++) {
        for(int k=0;k<colw[c]+2;k++) mvwaddch(g_pad, *y, curx++, '-');
        mvwaddch(g_pad, *y, curx++, '+');
    }
    (*y)++;
    for(int r=0;r<rows;r++) {
        curx = 0;
        mvwaddch(g_pad, *y, curx++, '|');
        for(int c=0;c<cols;c++) {
            mvwaddch(g_pad, *y, curx++, ' ');
            if(cells[r] && cells[r][c]) {
                mvwprintw(g_pad, *y, curx, "%-*s", colw[c], cells[r][c]);
                curx += colw[c];
            } else {
                for(int s=0;s<colw[c];s++) mvwaddch(g_pad, *y, curx++, ' ');
            }
            mvwaddch(g_pad, *y, curx++, ' ');
            mvwaddch(g_pad, *y, curx++, '|');
        }
        (*y)++;
        mvwaddch(g_pad, *y, 0, '+'); curx=1;
        for(int c=0;c<cols;c++) {
            for(int k=0;k<colw[c]+2;k++) mvwaddch(g_pad, *y, curx++, '-');
            mvwaddch(g_pad, *y, curx++, '+');
        }
        (*y)++;
    }
    for(int r=0;r<rows;r++) {
        if(cells[r]) {
            int c=0; while(cells[r][c]) free(cells[r][c++]);
            free(cells[r]);
        }
    }
    free(cells);
    free(colw);
}

/* render recursively; transparent tags simply render children */
static void render_node_recursive(Node *n, int *y, int indent) {
    if(!n) return;
    if(is_transparent(n->type)) {
        for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
        return;
    }
    switch(n->type) {
        case NODE_TEXT:
            if(n->text) render_wrapped_text(y, indent, n->text, 1 /*dim*/, 0 /*bold*/, 0);
            break;
        case NODE_BR:
            (*y)++;
            break;
        case NODE_HR:
            for(int x=0;x<g_term_w;x++) mvwaddch(g_pad, *y, x, '-');
            (*y)++;
            break;
        case NODE_PARAGRAPH:
            for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
            (*y)++;
            break;
        case NODE_HEADER: {
            /* collect text */
            size_t cap=256; char *buf = malloc(cap); buf[0]=0; size_t bl=0;
            for(int i=0;i<n->child_count;i++) {
                Node *c = n->children[i];
                if(c->type == NODE_TEXT && c->text) {
                    size_t need = strlen(c->text);
                    if(bl + need + 2 > cap) { cap = (bl + need + 2)*2; buf = realloc(buf, cap); }
                    if(bl) buf[bl++] = ' ';
                    strcpy(buf+bl, c->text); bl += need;
                }
            }
            if(bl>0) {
                wattron(g_pad, A_BOLD | COLOR_PAIR(1));
                mvwprintw(g_pad, *y, indent, "%s", buf);
                wattroff(g_pad, A_BOLD | COLOR_PAIR(1));
                (*y)++;
            }
            free(buf);
            break;
        }
        case NODE_PRE: {
            /* concat text children raw */
            size_t cap=256; char *buf = malloc(cap); buf[0]=0; size_t bl=0;
            for(int i=0;i<n->child_count;i++) if(n->children[i]->type == NODE_TEXT && n->children[i]->text) {
                size_t need = strlen(n->children[i]->text);
                if(bl + need + 2 > cap) { cap = (bl + need + 2)*2; buf = realloc(buf, cap); }
                strcpy(buf+bl, n->children[i]->text); bl += need;
            }
            render_code_block(y, buf);
            free(buf);
            break;
        }
        case NODE_CODE: {
            size_t cap=256; char *buf = malloc(cap); buf[0]=0; size_t bl=0;
            for(int i=0;i<n->child_count;i++) if(n->children[i]->type == NODE_TEXT && n->children[i]->text) {
                size_t need = strlen(n->children[i]->text);
                if(bl + need + 2 > cap) { cap = (bl + need + 2)*2; buf = realloc(buf, cap); }
                if(bl) buf[bl++] = '\n';
                strcpy(buf+bl, n->children[i]->text); bl += need;
            }
            render_code_block(y, buf);
            free(buf);
            break;
        }
        case NODE_BOLD:
            for(int i=0;i<n->child_count;i++) {
                if(n->children[i]->type == NODE_TEXT && n->children[i]->text) render_wrapped_text(y, indent, n->children[i]->text, 0, 1, 1);
                else render_node_recursive(n->children[i], y, indent);
            }
            break;
        case NODE_ITALIC:
            for(int i=0;i<n->child_count;i++) {
                if(n->children[i]->type == NODE_TEXT && n->children[i]->text) render_wrapped_text(y, indent, n->children[i]->text, 1, 0, 0);
                else render_node_recursive(n->children[i], y, indent);
            }
            break;
        case NODE_MARK:
            for(int i=0;i<n->child_count;i++) {
                if(n->children[i]->type == NODE_TEXT && n->children[i]->text) {
                    wattron(g_pad, COLOR_PAIR(4));
                    render_wrapped_text(y, indent, n->children[i]->text, 0, 0, 0);
                    wattroff(g_pad, COLOR_PAIR(4));
                } else render_node_recursive(n->children[i], y, indent);
            }
            break;
        case NODE_UNDER:
            for(int i=0;i<n->child_count;i++) {
                if(n->children[i]->type == NODE_TEXT && n->children[i]->text) {
                    wattron(g_pad, A_UNDERLINE);
                    render_wrapped_text(y, indent, n->children[i]->text, 0, 0, 0);
                    wattroff(g_pad, A_UNDERLINE);
                } else render_node_recursive(n->children[i], y, indent);
            }
            break;
        case NODE_STRIKE:
            {
                int start = *y;
                for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
                for(int ly = start; ly < *y; ly++) for(int cx = indent; cx < g_term_w; cx++) mvwaddch(g_pad, ly, cx, '-');
            }
            break;
        case NODE_BLOCKQUOTE:
            mvwaddstr(g_pad, *y, indent, " |"); (*y)++;
            for(int i=0;i<n->child_count;i++) {
                int before = *y;
                render_node_recursive(n->children[i], y, indent + 3);
                for(int ly = before; ly < *y; ly++) mvwprintw(g_pad, ly, indent, " | ");
            }
            mvwaddstr(g_pad, *y, indent, " |"); (*y)++;
            break;
        case NODE_UL:
            for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
            (*y)++;
            break;
        case NODE_OL:
            for(int i=0;i<n->child_count;i++) {
                Node *li = n->children[i];
                li->list_number = i+1;
                render_node_recursive(li, y, indent);
            }
            (*y)++;
            break;
        case NODE_LI:
            if(n->list_number > 0) {
                char tmp[32]; snprintf(tmp, sizeof(tmp), "%d. ", n->list_number);
                mvwprintw(g_pad, *y, indent, "%s", tmp);
                if(n->child_count && n->children[0]->type == NODE_TEXT && n->children[0]->text)
                    render_wrapped_text(y, indent + (int)strlen(tmp), n->children[0]->text, 1, 0, 0);
                else { (*y)++; for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent+4); }
            } else {
                wattron(g_pad, COLOR_PAIR(3));
                mvwprintw(g_pad, *y, indent, "* ");
                wattroff(g_pad, COLOR_PAIR(3));
                if(n->child_count && n->children[0]->type == NODE_TEXT && n->children[0]->text)
                    render_wrapped_text(y, indent+2, n->children[0]->text, 1, 0, 0);
                else { (*y)++; for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent+2); }
            }
            break;
        case NODE_DL:
            mvwprintw(g_pad, *y, indent, "Словник термінів"); (*y)++;
            for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
            (*y)++;
            break;
        case NODE_DT:
            if(n->child_count && n->children[0]->type==NODE_TEXT && n->children[0]->text) { mvwprintw(g_pad, *y, indent, "%s", n->children[0]->text); (*y)++; }
            break;
        case NODE_DD:
            if(n->child_count && n->children[0]->type==NODE_TEXT && n->children[0]->text) { mvwprintw(g_pad, *y, indent+4, "%s", n->children[0]->text); (*y)++; }
            break;
        case NODE_IMG: {
            const char *src = attr_get_value(n->attrs, "src");
            const char *alt = attr_get_value(n->attrs, "alt");
            char buf[1024];
            snprintf(buf, sizeof(buf), "[img: %s] %s", src?src:"(no-src)", alt?alt:"");
            wattron(g_pad, COLOR_PAIR(5));
            mvwprintw(g_pad, *y, indent, "%s", buf);
            wattroff(g_pad, COLOR_PAIR(5));
            (*y)++;
            break;
        }
        case NODE_FIGCAP:
            if(n->child_count && n->children[0]->type==NODE_TEXT && n->children[0]->text) {
                wattron(g_pad, A_STANDOUT);
                mvwprintw(g_pad, *y, indent, "%s", n->children[0]->text);
                wattroff(g_pad, A_STANDOUT);
                (*y)++;
            }
            break;
        case NODE_DETAILS: {
            Node *summary = NULL;
            for(int i=0;i<n->child_count;i++) if(n->children[i]->type==NODE_SUMMARY) summary = n->children[i];
            if(summary && summary->child_count && summary->children[0]->type==NODE_TEXT) {
                mvwprintw(g_pad, *y, indent, "> %s %s", summary->children[0]->text, n->expanded ? "(v)" : "(>)");
                (*y)++;
                if(n->expanded) for(int i=0;i<n->child_count;i++) if(n->children[i] != summary) render_node_recursive(n->children[i], y, indent+2);
            } else for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
            break;
        }
        case NODE_TABLE:
            render_table_node(y, n);
            break;
        case NODE_A: {
            size_t cap=256; char *buf = malloc(cap); buf[0]=0; size_t bl=0;
            for(int i=0;i<n->child_count;i++) if(n->children[i]->type==NODE_TEXT && n->children[i]->text) {
                size_t need = strlen(n->children[i]->text);
                if(bl + need + 2 > cap) { cap = (bl + need + 2)*2; buf = realloc(buf, cap); }
                if(bl) buf[bl++]=' ';
                strcpy(buf+bl, n->children[i]->text); bl += need;
            }
            if(bl==0) snprintf(buf, cap, "[link]");
            wattron(g_pad, COLOR_PAIR(2) | A_UNDERLINE);
            mvwprintw(g_pad, *y, indent, "%s", buf);
            wattroff(g_pad, COLOR_PAIR(2) | A_UNDERLINE);
            (*y)++;
            free(buf);
            break;
        }
        case NODE_FORM:
            mvwprintw(g_pad, *y, indent, "Form:"); (*y)++;
            for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent+2);
            break;
        case NODE_INPUT: {
            const char *nm = attr_get_value(n->attrs, "name");
            char lbl[256]; snprintf(lbl, sizeof(lbl), "%s: __________", nm?nm:"field");
            mvwprintw(g_pad, *y, indent, "%s", lbl); (*y)++;
            break;
        }
        case NODE_TEXTAREA: {
            const char *nm = attr_get_value(n->attrs, "name");
            char lbl[256]; snprintf(lbl, sizeof(lbl), "%s:", nm?nm:"textarea");
            mvwprintw(g_pad, *y, indent, "%s", lbl); (*y)++;
            mvwprintw(g_pad, *y, indent, "[");
            for(int k=0;k<g_term_w - indent - 4;k++) waddch(g_pad, '_');
            waddch(g_pad, ']'); (*y)++;
            break;
        }
        case NODE_BUTTON: {
            const char *val = attr_get_value(n->attrs, "value");
            const char *lab = val ? val : "Button";
            mvwprintw(g_pad, *y, indent, "[ %s ]", lab); (*y)++;
            break;
        }
        default:
            for(int i=0;i<n->child_count;i++) render_node_recursive(n->children[i], y, indent);
            break;
    }
}

/* render document into pad */
static void render_document(Node *root) {
    werase(g_pad);
    int y = 0;
    for(int i=0;i<root->child_count;i++) {
        render_node_recursive(root->children[i], &y, 0);
        if(y >= g_pad_h - 50) break;
    }
    g_pad_cur_y = y + 4;
}

/* ---------- builtin test HTML ---------- */
static const char *builtin_test_html =
"<!doctype html>\n<html>\n<head><meta charset=\"utf-8\"><title>Test</title></head>\n<body>\n"
"<header><h1>Мій браузер у терміналі</h1></header>\n"
"<main>\n<p>This is <strong>bold</strong>, <em>italic</em> and <mark>highlight</mark> text.</p>\n<hr>\n<pre><code>int main() {\n    printf(\"Hello, world!\\n\");\n}\n</code></pre>\n"
"<h2>Lists</h2>\n<ul><li>First</li><li>Second</li></ul>\n<ol><li>One</li><li>Two</li></ol>\n"
"<h2>Table</h2>\n<table><tr><th>№</th><th>Name</th><th>Age</th></tr><tr><td>1</td><td>Aleks</td><td>25</td></tr></table>\n"
"<p>Link: <a href=\"https://example.com\">Example</a></p>\n</main>\n<footer>Footer text</footer>\n</body>\n</html>\n";

/* ---------- main & UI ---------- */

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <url|file|test>\n", argv[0]);
        return 1;
    }

    char *arg = argv[1];
    char *raw = NULL;
    char *clean = NULL;
    Node *root = NULL;

    /* init curses */
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();

    /* colorpairs */
    init_pair(1, COLOR_WHITE, -1);  /* headers */
    init_pair(2, COLOR_BLUE, -1);   /* links */
    init_pair(3, COLOR_RED, -1);    /* bullets */
    init_pair(4, COLOR_BLACK, COLOR_YELLOW); /* mark highlight */
    init_pair(5, COLOR_MAGENTA, -1); /* images */

    getmaxyx(stdscr, g_term_h, g_term_w);
    g_pad_w = g_term_w;
    g_pad = newpad(g_pad_h, g_pad_w);
    if(!g_pad) { endwin(); fprintf(stderr, "Failed to create pad\n"); return 1; }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    int running = 1;
    int need_load = 1;
    char *current_arg = xstrdup(arg);
    static int top_pos = 0;

    while(running) {
        if(need_load) {
            /* free old DOM */
            if(root) { node_free_recursive(root); root = NULL; }
            /* get raw HTML */
            if(strcmp(current_arg, "test") == 0) raw = xstrdup(builtin_test_html);
            else raw = fetch_url(current_arg);
            if(!raw) {
                /* show error in a simple endwin + stderr, then exit */
                endwin();
                fprintf(stderr, "Failed to fetch '%s'\n", current_arg);
                free(current_arg);
                curl_global_cleanup();
                return 1;
            }
            sanitize_ascii_inplace(raw);
            clean = strip_script_style_meta(raw);
            free(raw); raw = NULL;
            if(!clean) {
                endwin();
                fprintf(stderr, "Failed to clean HTML\n");
                free(current_arg);
                curl_global_cleanup();
                return 1;
            }
            root = parse_html_tree(clean);
            free(clean); clean = NULL;
            /* render */
            getmaxyx(stdscr, g_term_h, g_term_w);
            /* recreate pad to match width */
            if(g_pad) { delwin(g_pad); g_pad = NULL; }
            g_pad = newpad(g_pad_h, g_term_w);
            g_pad_w = g_term_w;
            render_document(root);
            top_pos = 0;
            need_load = 0;
        }

        /* UI loop */
        while(1) {
            /* draw pad view */
            prefresh(g_pad, top_pos, 0, 0, 0, g_term_h - 2, g_term_w - 1);
            attron(A_REVERSE);
            mvprintw(g_term_h - 1, 0, "q=quit  r=reload  ↑/↓ scroll  PgUp/PgDn");
            clrtoeol();
            attroff(A_REVERSE);
            refresh();

            int ch = getch();
            if(ch == 'q' || ch == 'Q') { running = 0; break; }
            else if(ch == 'r' || ch == 'R') { need_load = 1; break; }
            else if(ch == KEY_UP) { if(top_pos > 0) top_pos--; }
            else if(ch == KEY_DOWN) { if(top_pos + g_term_h < g_pad_cur_y) top_pos++; }
            else if(ch == KEY_NPAGE) { top_pos += g_term_h - 2; if(top_pos + g_term_h > g_pad_cur_y) top_pos = g_pad_cur_y - g_term_h; if(top_pos < 0) top_pos = 0; }
            else if(ch == KEY_PPAGE) { top_pos -= g_term_h - 2; if(top_pos < 0) top_pos = 0; }
            else { /* ignore other keys */ }
        }
    }

    /* cleanup */
    if(root) node_free_recursive(root);
    if(current_arg) free(current_arg);
    if(g_pad) delwin(g_pad);
    endwin();
    curl_global_cleanup();
    return 0;
}