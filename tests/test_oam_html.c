/*
 * GFOpenMail's HTML-to-text converter as GFOpenBrowser Lite uses it for its
 * Links... list: https://example.com/ (no </head>) keeps its text and link.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oam_buf.h"
#include "oam_html.h"

#include <stdio.h>
#include <string.h>

static int failed;

#define CHECK(cond) do { if (!(cond)) { printf("OAMHTML FAIL line %d: %s\n", __LINE__, #cond); failed = 1; } } while (0)

static void page(const char *name, const char *html)
{
    oam_buf text, links;
    const char *heading;
    oam_buf_init(&text);
    oam_buf_init(&links);
    CHECK(oam_html_to_text(html, strlen(html), &text, &links));
    CHECK(strstr(oam_buf_str(&text), "Example Domain") != NULL);
    CHECK(strstr(oam_buf_str(&text), "documentation examples") != NULL);
    CHECK(strstr(oam_buf_str(&text), "Learn more [1]") != NULL);
    CHECK(!strcmp(oam_buf_str(&links), "https://iana.org/domains/example\n"));
    CHECK(strstr(oam_buf_str(&text), "background") == NULL);    /* not the <style> */
    heading = strstr(oam_buf_str(&text), "Example Domain");
    CHECK(heading && !strstr(heading + 1, "Example Domain"));   /* the heading, not the <title> too */
    if (failed) printf("OAMHTML %s:\n%s\n", name, oam_buf_str(&text));
    oam_buf_free(&text);
    oam_buf_free(&links);
}

int main(void)
{
    page("example.com",
         "<!doctype html><html lang=\"en\"><head><title>Example Domain</title>"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
         "<style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}"
         "h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style>"
         "<body><div><h1>Example Domain</h1><p>This domain is for use in documentation "
         "examples without needing permission. Avoid use in operations.<p>"
         "<a href=\"https://iana.org/domains/example\">Learn more</a></div></body></html>\n");
    page("example.com (closed)",
         "<!doctype html>\n<html lang=\"en\">\n<head>\n<title>Example Domain</title>\n"
         "<meta charset=\"utf-8\">\n<style>body{background:#eee}</style>\n</head>\n"
         "<body><div><h1>Example Domain</h1><p>This domain is for use in documentation "
         "examples without needing permission.</p>"
         "<p><a href=\"https://iana.org/domains/example\">Learn more</a></p></div></body>\n</html>\n");
    printf("OAMHTML %s\n", failed ? "FAIL" : "PASS");
    return failed;
}
