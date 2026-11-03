/*
 * Where a page's time goes (ob_profile.h): JavaScriptCore's and WebCore's
 * OBProfileScope hooks (WebKit patch 0095), timed here. Each scope's own
 * time is its whole time less the scopes inside it, so a compile inside a
 * script's run counts as compiling, and a style update inside a call as
 * style. Compiles, top-level runs and calls (events, timers: into a
 * JavaScript function, by its script) are kept by the script's URL; calls
 * into native functions, style and layout as totals.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include "config.h"
#include "ob_profile.h"

#include <cstddef>
#include <wtf/Vector.h>
#include <JavaScriptCore/Completion.h>
#include <JavaScriptCore/SourceProvider.h>
#include <algorithm>
#include <stdio.h>
#include <wtf/HashMap.h>
#include <wtf/MonotonicTime.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/Vector.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

namespace {

struct Frame {
    int kind;
    const JSC::SourceProvider* provider;
    MonotonicTime start;
    double inner { 0 };
};

struct Script {
    double compileMs { 0 };
    double runMs { 0 };
    double callMs { 0 };
    unsigned calls { 0 };
    unsigned compiles { 0 };
    unsigned runs { 0 };
};

struct Profile {
    Vector<Frame, 16> stack;
    HashMap<String, Script> scripts;
    double totals[5] { };
    unsigned counts[5] { };
};

Profile& profile()
{
    static NeverDestroyed<Profile> p;
    return p;
}

String urlOf(const JSC::SourceProvider* provider)
{
    if (!provider)
        return "(none)"_s;
    String url = provider->sourceURL();
    return url.isEmpty() ? "(inline)"_s : url;
}

void begin(int kind, const JSC::SourceProvider* provider)
{
    profile().stack.append({ kind, provider, MonotonicTime::now() });
}

void end(int kind, const JSC::SourceProvider*)
{
    auto& p = profile();
    if (p.stack.isEmpty())
        return;
    Frame frame = p.stack.takeLast();
    if (frame.kind != kind)
        return;
    double whole = (MonotonicTime::now() - frame.start).milliseconds();
    double own = std::max(0.0, whole - frame.inner);
    if (!p.stack.isEmpty())
        p.stack.last().inner += whole;
    p.totals[kind] += own;
    p.counts[kind]++;
    if (kind == JSC::OBProfileCall && frame.provider) {
        auto& script = p.scripts.add(urlOf(frame.provider), Script { }).iterator->value;
        script.callMs += own;
        script.calls++;
    } else if (kind == JSC::OBProfileCompile || kind == JSC::OBProfileEvaluate) {
        auto& script = p.scripts.add(urlOf(frame.provider), Script { }).iterator->value;
        if (kind == JSC::OBProfileCompile) {
            script.compileMs += own;
            script.compiles++;
        } else {
            script.runMs += own;
            script.runs++;
        }
    }
}

} // namespace

extern "C" void ob_profile_start(void)
{
    JSC::obProfileHooks.begin = begin;
    JSC::obProfileHooks.end = end;
}

extern "C" void ob_profile_report(void)
{
    auto& p = profile();
    // for mapping a run's PCs (AC_PROFILE's guest blocks) to functions: an
    // address in the program and one in the JavaScriptEngine image
    printf("OBVIEW_ADDR ob_profile_start=%p JSC::obProfileHooks=%p\n", reinterpret_cast<void*>(ob_profile_start),
        static_cast<void*>(&JSC::obProfileHooks));
    static const char* const names[] = { "compile", "run", "calls", "style", "layout" };
    printf("OBVIEW_PROFILE");
    for (int k = 0; k < 5; k++)
        printf(" %s_ms=%.0f %s_n=%u", names[k], p.totals[k], names[k], p.counts[k]);
    printf("\n");
    Vector<std::pair<String, Script>> rows;
    for (auto& entry : p.scripts)
        rows.append({ entry.key, entry.value });
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
        return a.second.compileMs + a.second.runMs + a.second.callMs > b.second.compileMs + b.second.runMs + b.second.callMs;
    });
    for (auto& row : rows) {
        printf("OBVIEW_SCRIPT compile_ms=%.1f compiles=%u run_ms=%.1f runs=%u call_ms=%.1f calls=%u %s\n", row.second.compileMs,
            row.second.compiles, row.second.runMs, row.second.runs, row.second.callMs, row.second.calls, row.first.utf8().data());
    }
    fflush(stdout);
}
