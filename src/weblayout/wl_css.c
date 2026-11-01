/*
 * weblayout: the small CSS cascade (CSS Lite).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "wl_css.h"

#include <string.h>

#include "wl_libc.h"

#define OB_CSS_CLASS_MAX 4

struct wl_css_rule {
    char tag[20];
    char id[64];
    char classes[OB_CSS_CLASS_MAX][40];
    unsigned nclasses;
    unsigned specificity;
    unsigned order;
    char *decl;
};

static char *range_dup(wl_pool *pool, const char *s, size_t n)
{
    return wl_strndup(pool, s, n);
}

static char *trim(char *s)
{
    char *e;
    while (*s && wl_isspace((unsigned char)*s)) ++s;
    e = s + strlen(s);
    while (e > s && wl_isspace((unsigned char)e[-1])) --e;
    *e = 0;
    return s;
}

static void lower_ascii(char *s)
{
    while (*s) {
        *s = (char)wl_tolower((unsigned char)*s);
        ++s;
    }
}

void wl_css_init(wl_css_sheet *sheet)
{
    if (sheet) memset(sheet, 0, sizeof(*sheet));
}

void wl_css_free(wl_css_sheet *sheet)
{
    size_t i;
    wl_pool *pool;
    if (!sheet) return;
    pool = sheet->pool;
    for (i = 0; i < sheet->count; ++i) wl_free(sheet->rules[i].decl);
    wl_free(sheet->rules);
    memset(sheet, 0, sizeof(*sheet));
    sheet->pool = pool;
}

size_t wl_css_rule_count(const wl_css_sheet *sheet)
{
    return sheet ? sheet->count : 0;
}

static int reserve_rules(wl_css_sheet *sheet, size_t extra)
{
    size_t need, cap;
    wl_css_rule *p;
    need = sheet->count + extra;
    if (need <= sheet->capacity) return 1;
    cap = sheet->capacity ? sheet->capacity : 16;
    while (cap < need) cap *= 2;
    p = (wl_css_rule *)wl_realloc(sheet->pool, sheet->rules, cap * sizeof(*p));
    if (!p) return 0;
    sheet->rules = p;
    sheet->capacity = cap;
    return 1;
}

static int ident_char(int c)
{
    return wl_isalnum((unsigned char)c) || c == '-' || c == '_';
}

static int parse_selector(const char *text, wl_css_rule *rule)
{
    const char *p = text;
    char *dst;
    size_t n;

    memset(rule, 0, sizeof(*rule));
    while (*p && wl_isspace((unsigned char)*p)) ++p;
    if (!*p) return 0;

    if (*p == '*') {
        ++p;
    } else if (wl_isalpha((unsigned char)*p)) {
        dst = rule->tag;
        n = 0;
        while (*p && ident_char(*p) && n + 1 < sizeof(rule->tag))
            dst[n++] = (char)wl_tolower((unsigned char)*p++);
        dst[n] = 0;
        rule->specificity += 1;
        while (*p && ident_char(*p)) ++p;
    }

    while (*p) {
        if (*p == '#') {
            ++p;
            n = 0;
            while (*p && ident_char(*p)) {
                if (n + 1 < sizeof(rule->id)) rule->id[n++] = *p;
                ++p;
            }
            rule->id[n] = 0;
            if (!n) return 0;
            rule->specificity += 100;
        } else if (*p == '.') {
            unsigned ci;
            ++p;
            ci = rule->nclasses;
            n = 0;
            while (*p && ident_char(*p)) {
                if (ci < OB_CSS_CLASS_MAX && n + 1 < sizeof(rule->classes[0]))
                    rule->classes[ci][n++] = *p;
                ++p;
            }
            if (!n) return 0;
            if (ci < OB_CSS_CLASS_MAX) {
                rule->classes[ci][n] = 0;
                ++rule->nclasses;
            }
            rule->specificity += 10;
        } else if (wl_isspace((unsigned char)*p)) {
            while (*p && wl_isspace((unsigned char)*p)) ++p;
            if (*p) return 0; /* no descendant/child combinators in Lite CSS yet */
        } else {
            return 0; /* pseudo, attribute and combinator selectors are not guessed */
        }
    }
    return rule->tag[0] || rule->id[0] || rule->nclasses ||
           rule->specificity == 0;
}

static int add_rule(wl_css_sheet *sheet, const char *selector,
                    const char *decl, size_t decl_len)
{
    wl_css_rule rule;
    char *sel, *body;
    if (!sheet || !selector || !decl) return 0;
    sel = range_dup(sheet->pool, selector, strlen(selector));
    if (!sel) return 0;
    if (!parse_selector(trim(sel), &rule)) {
        wl_free(sel);
        return 1; /* unsupported selector: ignore safely */
    }
    wl_free(sel);
    body = range_dup(sheet->pool, decl, decl_len);
    if (!body) return 0;
    if (!reserve_rules(sheet, 1)) {
        wl_free(body);
        return 0;
    }
    rule.order = ++sheet->order;
    rule.decl = body;
    sheet->rules[sheet->count++] = rule;
    return 1;
}

static const char *skip_comment(const char *p, const char *end)
{
    if (p + 1 < end && p[0] == '/' && p[1] == '*') {
        p += 2;
        while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) ++p;
        if (p + 1 < end) p += 2;
    }
    return p;
}

int wl_css_add(wl_css_sheet *sheet, const char *css, size_t length)
{
    const char *p, *end;
    if (!sheet || !css) return 0;
    p = css;
    end = css + length;

    while (p < end) {
        const char *sel_start, *brace, *body_start, *close, *q, *part;
        while (p < end) {
            if (p + 1 < end && p[0] == '/' && p[1] == '*') p = skip_comment(p, end);
            else if (wl_isspace((unsigned char)*p)) ++p;
            else break;
        }
        if (p >= end) break;

        sel_start = p;
        brace = p;
        while (brace < end && *brace != '{') {
            if (brace + 1 < end && brace[0] == '/' && brace[1] == '*')
                brace = skip_comment(brace, end);
            else
                ++brace;
        }
        if (brace >= end) break;
        body_start = brace + 1;
        close = body_start;
        while (close < end && *close != '}') ++close;
        if (close >= end) break;

        if (*sel_start != '@') {
            part = sel_start;
            q = sel_start;
            while (q <= brace) {
                if (q == brace || *q == ',') {
                    char *one = range_dup(sheet->pool, part, (size_t)(q - part));
                    if (!one) return 0;
                    if (!add_rule(sheet, trim(one), body_start,
                                  (size_t)(close - body_start))) {
                        wl_free(one);
                        return 0;
                    }
                    wl_free(one);
                    part = q + 1;
                }
                ++q;
            }
        }
        p = close + 1;
    }
    return 1;
}

static int class_has(const char *classes, const char *want)
{
    const char *p;
    size_t n;
    if (!want || !*want) return 1;
    if (!classes) return 0;
    n = strlen(want);
    p = classes;
    while (*p) {
        const char *s;
        while (*p && wl_isspace((unsigned char)*p)) ++p;
        s = p;
        while (*p && !wl_isspace((unsigned char)*p)) ++p;
        if ((size_t)(p - s) == n && !memcmp(s, want, n)) return 1;
    }
    return 0;
}

static int rule_matches(const wl_css_rule *r, const char *tag,
                        const char *id, const char *classes)
{
    unsigned i;
    if (r->tag[0] && (!tag || wl_strcasecmp(r->tag, tag))) return 0;
    if (r->id[0] && (!id || strcmp(r->id, id))) return 0;
    for (i = 0; i < r->nclasses; ++i)
        if (!class_has(classes, r->classes[i])) return 0;
    return 1;
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = wl_tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "r , g , b" as sscanf's "%d , %d , %d" reads it */
static int rgb_triplet(const char *v, int *r, int *g, int *b)
{
    int *out[3];
    int i;
    char *end;
    out[0] = r; out[1] = g; out[2] = b;
    for (i = 0; i < 3; ++i) {
        long n;
        if (i) {
            while (wl_isspace((unsigned char)*v)) ++v;
            if (*v++ != ',') return 0;
        }
        n = wl_strtol(v, &end, 10);
        if (end == v) return 0;
        if (n > 100000) n = 100000;
        if (n < -100000) n = -100000;
        *out[i] = (int)n;
        v = end;
    }
    return 1;
}

static int parse_color(const char *v, uint32_t *argb)
{
    int a, b, c, d, e, f;
    int r, g, bl;
    char name[32];
    size_t n;

    while (*v && wl_isspace((unsigned char)*v)) ++v;
    if (*v == '#') {
        ++v;
        n = strlen(v);
        if (n == 3) {
            a = hexv(v[0]); b = hexv(v[1]); c = hexv(v[2]);
            if (a < 0 || b < 0 || c < 0) return 0;
            r = a * 17; g = b * 17; bl = c * 17;
        } else if (n >= 6) {
            a = hexv(v[0]); b = hexv(v[1]); c = hexv(v[2]);
            d = hexv(v[3]); e = hexv(v[4]); f = hexv(v[5]);
            if (a < 0 || b < 0 || c < 0 || d < 0 || e < 0 || f < 0) return 0;
            r = a * 16 + b; g = c * 16 + d; bl = e * 16 + f;
        } else return 0;
        *argb = 0xff000000u | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | (uint32_t)bl;
        return 1;
    }

    if (!wl_strncasecmp(v, "rgb(", 4)) {
        if (rgb_triplet(v + 4, &r, &g, &bl)) {
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (bl < 0) bl = 0;
            if (bl > 255) bl = 255;
            *argb = 0xff000000u | ((uint32_t)r << 16) |
                    ((uint32_t)g << 8) | (uint32_t)bl;
            return 1;
        }
    }

    n = 0;
    while (v[n] && !wl_isspace((unsigned char)v[n]) && n + 1 < sizeof(name)) {
        name[n] = (char)wl_tolower((unsigned char)v[n]);
        ++n;
    }
    name[n] = 0;
    if (!strcmp(name, "transparent")) { *argb = 0; return 1; }
    if (!strcmp(name, "black"))   { *argb = 0xff000000u; return 1; }
    if (!strcmp(name, "white"))   { *argb = 0xffffffffu; return 1; }
    if (!strcmp(name, "red"))     { *argb = 0xffff0000u; return 1; }
    if (!strcmp(name, "green"))   { *argb = 0xff008000u; return 1; }
    if (!strcmp(name, "blue"))    { *argb = 0xff0000ffu; return 1; }
    if (!strcmp(name, "yellow"))  { *argb = 0xffffff00u; return 1; }
    if (!strcmp(name, "gray") || !strcmp(name, "grey")) { *argb = 0xff808080u; return 1; }
    if (!strcmp(name, "silver"))  { *argb = 0xffc0c0c0u; return 1; }
    if (!strcmp(name, "navy"))    { *argb = 0xff000080u; return 1; }
    if (!strcmp(name, "maroon"))  { *argb = 0xff800000u; return 1; }
    if (!strcmp(name, "purple"))  { *argb = 0xff800080u; return 1; }
    if (!strcmp(name, "teal"))    { *argb = 0xff008080u; return 1; }
    if (!strcmp(name, "orange"))  { *argb = 0xffffa500u; return 1; }
    return 0;
}

static int parse_decimal(const char *v, const char **end_out,
                         long *num_out, long *den_out)
{
    const char *p = v;
    long whole = 0, frac = 0, den = 1;
    int sign = 1, digits = 0, frac_digits = 0;

    while (*p && wl_isspace((unsigned char)*p)) ++p;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') ++p;

    while (wl_isdigit((unsigned char)*p)) {
        whole = whole * 10 + (*p++ - '0');
        digits = 1;
    }
    if (*p == '.') {
        ++p;
        while (wl_isdigit((unsigned char)*p)) {
            if (frac_digits < 4) {
                frac = frac * 10 + (*p - '0');
                den *= 10;
                ++frac_digits;
            }
            ++p;
            digits = 1;
        }
    }
    if (!digits) return 0;
    *num_out = sign * (whole * den + frac);
    *den_out = den;
    *end_out = p;
    return 1;
}

static int parse_length(const char *v, ol_unit basis, ol_unit *out)
{
    const char *end;
    long num, den;
    int64_t value;
    while (*v && wl_isspace((unsigned char)*v)) ++v;
    if (!wl_strcasecmp(v, "thin")) { *out = OL_CSSPX(1); return 1; }
    if (!wl_strcasecmp(v, "medium")) { *out = OL_CSSPX(3); return 1; }
    if (!wl_strcasecmp(v, "thick")) { *out = OL_CSSPX(5); return 1; }
    if (!parse_decimal(v, &end, &num, &den)) return 0;
    while (*end && wl_isspace((unsigned char)*end)) ++end;
    if (!*end || !wl_strcasecmp(end, "px"))
        value = (int64_t)num * OL_UNITS_PER_CSSPX / den;
    else if (!wl_strcasecmp(end, "pt"))
        value = (int64_t)num * OL_UNITS_PER_POINT / den;
    else if (!wl_strcasecmp(end, "em"))
        value = (int64_t)num * basis / den;
    else if (!strcmp(end, "%"))
        value = (int64_t)num * basis / (den * 100L);
    else
        return 0;
    *out = (ol_unit)value;
    return 1;
}

static int parse_font_size(const char *v, ol_style *s, ol_unit *out)
{
    if (!wl_strcasecmp(v, "xx-small")) { *out = OL_CSSPX(9); return 1; }
    if (!wl_strcasecmp(v, "x-small"))  { *out = OL_CSSPX(10); return 1; }
    if (!wl_strcasecmp(v, "small"))    { *out = OL_CSSPX(13); return 1; }
    if (!wl_strcasecmp(v, "medium"))   { *out = OL_CSSPX(16); return 1; }
    if (!wl_strcasecmp(v, "large"))    { *out = OL_CSSPX(18); return 1; }
    if (!wl_strcasecmp(v, "x-large"))  { *out = OL_CSSPX(24); return 1; }
    if (!wl_strcasecmp(v, "xx-large")) { *out = OL_CSSPX(32); return 1; }
    if (!wl_strcasecmp(v, "smaller"))  { *out = s->font_size * 4 / 5; return 1; }
    if (!wl_strcasecmp(v, "larger"))   { *out = s->font_size * 6 / 5; return 1; }
    return parse_length(v, s->font_size, out);
}

static int split_lengths(const char *value, ol_style *s, ol_unit out[4], int *n)
{
    char buf[160], *p, *tok;
    int count = 0;
    size_t len = strlen(value);
    if (len >= sizeof(buf)) return 0;
    memcpy(buf, value, len + 1);
    p = buf;
    while (*p && count < 4) {
        char *start;
        while (*p && wl_isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        start = p;
        while (*p && !wl_isspace((unsigned char)*p)) ++p;
        if (*p) *p++ = 0;
        tok = start;
        if (!parse_length(tok, s->font_size, &out[count])) return 0;
        ++count;
    }
    if (!count) return 0;
    *n = count;
    return 1;
}

static void apply_box(ol_style *s, int padding, const char *value)
{
    ol_unit v[4], t, r, b, l;
    int n;
    if (!split_lengths(value, s, v, &n)) return;
    if (n == 1) t = r = b = l = v[0];
    else if (n == 2) { t = b = v[0]; r = l = v[1]; }
    else if (n == 3) { t = v[0]; r = l = v[1]; b = v[2]; }
    else { t = v[0]; r = v[1]; b = v[2]; l = v[3]; }
    if (padding) {
        s->padding_top = t; s->padding_right = r;
        s->padding_bottom = b; s->padding_left = l;
    } else {
        s->margin_top = t; s->margin_right = r;
        s->margin_bottom = b; s->margin_left = l;
    }
}

static void apply_property(ol_style *s, char *name, char *value)
{
    ol_unit u;
    uint32_t color;
    char *bang;

    name = trim(name);
    value = trim(value);
    lower_ascii(name);
    bang = strstr(value, "!important");
    if (bang) {
        *bang = 0;
        value = trim(value);
    }

    if (!strcmp(name, "color")) {
        if (parse_color(value, &color)) s->foreground = color;
    } else if (!strcmp(name, "background-color") || !strcmp(name, "background")) {
        if (parse_color(value, &color)) s->background = color;
    } else if (!strcmp(name, "font-size")) {
        if (parse_font_size(value, s, &u) && u > 0) s->font_size = u;
    } else if (!strcmp(name, "line-height")) {
        if (!wl_strcasecmp(value, "normal")) {
            s->line_height = s->font_size * 5 / 4;
        } else {
            const char *e;
            long num, den;
            if (parse_decimal(value, &e, &num, &den)) {
                while (*e && wl_isspace((unsigned char)*e)) ++e;
                if (!*e && num > 0)
                    s->line_height = (ol_unit)((int64_t)num * s->font_size / den);
                else if (parse_length(value, s->font_size, &u) && u > 0)
                    s->line_height = u;
            } else if (parse_length(value, s->font_size, &u) && u > 0) {
                s->line_height = u;
            }
        }
    } else if (!strcmp(name, "font-weight")) {
        if (!wl_strcasecmp(value, "bold") || !wl_strcasecmp(value, "bolder") ||
            wl_atoi(value) >= 600) s->text_flags |= OL_TEXT_BOLD;
        else if (!wl_strcasecmp(value, "normal") || !wl_strcasecmp(value, "lighter"))
            s->text_flags &= ~OL_TEXT_BOLD;
    } else if (!strcmp(name, "font-style")) {
        if (!wl_strcasecmp(value, "italic") || !wl_strcasecmp(value, "oblique"))
            s->text_flags |= OL_TEXT_ITALIC;
        else if (!wl_strcasecmp(value, "normal"))
            s->text_flags &= ~OL_TEXT_ITALIC;
    } else if (!strcmp(name, "text-decoration")) {
        if (strstr(value, "underline")) s->text_flags |= OL_TEXT_UNDERLINE;
        else if (!wl_strcasecmp(value, "none")) s->text_flags &= ~OL_TEXT_UNDERLINE;
    } else if (!strcmp(name, "font-family")) {
        if (strstr(value, "monospace") || strstr(value, "Monospace") ||
            strstr(value, "Courier") || strstr(value, "courier"))
            s->text_flags |= OL_TEXT_MONO;
    } else if (!strcmp(name, "text-align")) {
        if (!wl_strcasecmp(value, "center")) s->text_align = OL_ALIGN_CENTER;
        else if (!wl_strcasecmp(value, "right") || !wl_strcasecmp(value, "end"))
            s->text_align = OL_ALIGN_END;
        else if (!wl_strcasecmp(value, "left") || !wl_strcasecmp(value, "start"))
            s->text_align = OL_ALIGN_START;
    } else if (!strcmp(name, "display")) {
        if (!wl_strcasecmp(value, "none")) s->display = OL_DISPLAY_NONE;
        else if (!wl_strcasecmp(value, "block")) s->display = OL_DISPLAY_BLOCK;
        else if (!wl_strcasecmp(value, "inline") || !wl_strcasecmp(value, "inline-block"))
            s->display = OL_DISPLAY_INLINE;
    } else if (!strcmp(name, "margin")) {
        apply_box(s, 0, value);
    } else if (!strcmp(name, "padding")) {
        apply_box(s, 1, value);
    } else if (!strcmp(name, "margin-top")) {
        if (parse_length(value, s->font_size, &u)) s->margin_top = u;
    } else if (!strcmp(name, "margin-right")) {
        if (parse_length(value, s->font_size, &u)) s->margin_right = u;
    } else if (!strcmp(name, "margin-bottom")) {
        if (parse_length(value, s->font_size, &u)) s->margin_bottom = u;
    } else if (!strcmp(name, "margin-left")) {
        if (parse_length(value, s->font_size, &u)) s->margin_left = u;
    } else if (!strcmp(name, "padding-top")) {
        if (parse_length(value, s->font_size, &u)) s->padding_top = u;
    } else if (!strcmp(name, "padding-right")) {
        if (parse_length(value, s->font_size, &u)) s->padding_right = u;
    } else if (!strcmp(name, "padding-bottom")) {
        if (parse_length(value, s->font_size, &u)) s->padding_bottom = u;
    } else if (!strcmp(name, "padding-left")) {
        if (parse_length(value, s->font_size, &u)) s->padding_left = u;
    }
}

static void apply_declarations(wl_pool *pool, ol_style *style, const char *decl)
{
    const char *p = decl;
    while (*p) {
        const char *colon, *semi;
        char *name, *value;
        while (*p && (wl_isspace((unsigned char)*p) || *p == ';')) ++p;
        if (!*p) break;
        colon = strchr(p, ':');
        if (!colon) break;
        semi = strchr(colon + 1, ';');
        if (!semi) semi = p + strlen(p);
        name = range_dup(pool, p, (size_t)(colon - p));
        value = range_dup(pool, colon + 1, (size_t)(semi - colon - 1));
        if (!name || !value) {
            wl_free(name); wl_free(value);
            return;
        }
        apply_property(style, name, value);
        wl_free(name); wl_free(value);
        p = *semi ? semi + 1 : semi;
    }
}

void wl_css_apply(const wl_css_sheet *sheet,
                  const char *tag, const char *id, const char *classes,
                  const char *inline_style, ol_style *style)
{
    unsigned last_spec = 0, last_order = 0;
    int have_last = 0;
    if (!style) return;

    if (sheet) {
        for (;;) {
            size_t i, best = (size_t)-1;
            unsigned best_spec = 0, best_order = 0;
            for (i = 0; i < sheet->count; ++i) {
                const wl_css_rule *r = &sheet->rules[i];
                int after = !have_last ||
                    r->specificity > last_spec ||
                    (r->specificity == last_spec && r->order > last_order);
                if (!after || !rule_matches(r, tag, id, classes)) continue;
                if (best == (size_t)-1 ||
                    r->specificity < best_spec ||
                    (r->specificity == best_spec && r->order < best_order)) {
                    best = i;
                    best_spec = r->specificity;
                    best_order = r->order;
                }
            }
            if (best == (size_t)-1) break;
            apply_declarations(sheet->pool, style, sheet->rules[best].decl);
            last_spec = best_spec;
            last_order = best_order;
            have_last = 1;
        }
    }

    if (inline_style && *inline_style)
        apply_declarations(sheet ? sheet->pool : NULL, style, inline_style);
}
