/* weblayout's address resolver on the host: tests/run-host-tests.sh.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "wl_url.h"

#include <stdio.h>
#include <string.h>

static int failed;

static void check(const char *base, const char *href, const char *want)
{
    char out[256];
    wl_url_resolve(base, href, out, sizeof out);
    if (strcmp(out, want)) {
        printf("WLURL FAIL base=%s href=%s got=%s want=%s\n", base, href, out, want);
        failed = 1;
    }
}

int main(void)
{
    const char *b = "https://example.com/a/b/page.html?q=1#top";
    check(b, "https://other.org/x", "https://other.org/x");
    check(b, "mailto:someone@example.com", "mailto:someone@example.com");
    check(b, "//cdn.example.net/s.css", "https://cdn.example.net/s.css");
    check(b, "/root.css", "https://example.com/root.css");
    check(b, "/a/./b/../c.css", "https://example.com/a/c.css");
    check(b, "style.css", "https://example.com/a/b/style.css");
    check(b, "./style.css", "https://example.com/a/b/style.css");
    check(b, "../up.png", "https://example.com/a/up.png");
    check(b, "../../../../over.png", "https://example.com/over.png");
    check(b, "?q=2", "https://example.com/a/b/page.html?q=2");
    check(b, "#later", "https://example.com/a/b/page.html?q=1#later");
    check(b, "", "https://example.com/a/b/page.html?q=1");
    check(b, "  spaced.html", "https://example.com/a/b/spaced.html");
    check(b, "dir/?u=http://x.org/", "https://example.com/a/b/dir/?u=http://x.org/");
    check("http://example.com", "x.html", "http://example.com/x.html");
    check("http://example.com:8080/d/", "e.html", "http://example.com:8080/d/e.html");
    check("example.com/d/f", "g", "http://example.com/d/g");
    check(NULL, "rel.html", "rel.html");
    {
        char small[8];
        if (wl_url_resolve(b, "long-name.html", small, sizeof small) || strlen(small) != 7) {
            printf("WLURL FAIL a cut answer is not reported\n");
            failed = 1;
        }
    }
    if (!failed) printf("WLURL PASS\n");
    return failed ? 20 : 0;
}
