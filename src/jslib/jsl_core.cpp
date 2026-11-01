/*
 * javascript.library engine: the core, on JavaScriptCore's C interface.
 *
 * A JSTask is a Task here: a script thread (jsl_os_spawn) with its own
 * context group, so its own VM, heap and global object. Every engine call
 * is run on that thread (run()): from another task it is handed over and
 * waited for, from the script thread itself (a host class's function
 * calling back) it runs at once.
 *
 * Host classes: each class gets a prototype object holding its slots,
 * chained to its parent's. A method is a callable object of fnClass whose
 * private data says which slot; an accessor or a field is a getter (and
 * setter) pair of those, put on with Object.defineProperty. A program
 * object's wrapper is an object of wrapperClass with the class's prototype;
 * one wrapper per program pointer, found again through a weak reference,
 * so a wrapper the program no longer reaches is collected, and the class's
 * release function is told.
 *
 * Values given to programs are JSValueRefs held by JSValueProtect; JS_Free
 * lets them go. NULL is undefined.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <JavaScriptCore/JavaScript.h>
#include <JavaScriptCore/JSWeakPrivate.h>

#include <atomic>
#include <cstddef>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "jsl_cache.h"
#include "jsl_engine.h"
#include "jsl_os.h"

namespace {

constexpr unsigned long defaultStack = 512 * 1024;
constexpr size_t errorSize = 512;
std::atomic<long> liveTasks { 0 };

struct Task;

struct ClassRec {
    ClassRec *next;
    const JSHostClass *host;
    ClassRec *parent;
    JSObjectRef proto;              // protected
};

struct Wrapper {
    Task *task;
    ClassRec *rec;
    void *object;
    JSWeakRef weak;
};

enum Role { RoleFunction, RoleMethod, RoleGet, RoleSet, RoleField };

struct FnData {
    Task *task;
    Role role;
    ClassRec *rec;                  // the slot's class (methods, accessors, fields)
    const JSHostSlot *slot;
    JSMethodFunc func;              // RoleFunction
    void *data;                     // RoleFunction
};

struct Task {
    const JSEngineTable *table;     // first: struct JSTaskHead
    jsl_box *box = nullptr;
    JSContextGroupRef group = nullptr;
    JSGlobalContextRef ctx = nullptr;
    JSClassRef fnClass = nullptr;
    JSClassRef wrapperClass = nullptr;
    JSObjectRef defineAccessor = nullptr;   // protected
    JSObjectRef functionPrototype = nullptr; // protected: host functions are functions
    JSObjectRef typeError = nullptr;        // protected: the TypeError constructor
    APTR user = nullptr;
    unsigned long stack = defaultStack;
    char name[32] = "JavaScript";
    char cacheDir[256] = "";                // translations kept here (JSA_CacheDir)
    JSLCacheList *cache = nullptr;          // scripts with translations to write
    JSValueRef pending = nullptr;           // protected: thrown when the slot function returns
    ClassRec *classes = nullptr;
    std::unordered_map<void *, Wrapper *> wrappers;     // the live wrapper of each program object
    std::unordered_set<Wrapper *> allWrappers;          // and older ones not yet finalized
    bool quit = false;
    char error[errorSize] = "";
};

const JSEngineTable *engineTable();

// ---- Running on the script thread ------------------------------------------

template<typename F> void trampoline(void *arg) { (*static_cast<F *>(arg))(); }

template<typename F> void run(Task *t, F &&f)
{
    if (jsl_os_is_owner(t->box)) {
        f();
        return;
    }
    jsl_call call;
    memset(&call, 0, sizeof call);
    call.fn = trampoline<std::remove_reference_t<F>>;
    call.arg = &f;
    jsl_os_send(t->box, &call);
}

// Starting and ending tasks: on the engine's own process (jsl_os_on_engine)
template<typename F> void onEngine(F &&f)
{
    jsl_call call;
    memset(&call, 0, sizeof call);
    call.fn = trampoline<std::remove_reference_t<F>>;
    call.arg = &f;
    jsl_os_on_engine(&call);
}

Task *task(JSTask t) { return static_cast<Task *>(t); }

// ---- Values ------------------------------------------------------------------

JSValueRef in(Task *t, JSValue v)
{
    return v ? static_cast<JSValueRef>(v) : JSValueMakeUndefined(t->ctx);
}

// A value for the program: kept alive until JS_Free; undefined is NULL
JSValue out(Task *t, JSValueRef v)
{
    if (!v || JSValueIsUndefined(t->ctx, v))
        return nullptr;
    JSValueProtect(t->ctx, v);
    return const_cast<OpaqueJSValue *>(v);
}

JSStringRef makeString(const char *utf8, long length)
{
    if (length < 0)
        return JSStringCreateWithUTF8CString(utf8 ? utf8 : "");
    char *copy = static_cast<char *>(malloc(length + 1));
    if (!copy)
        return nullptr;
    memcpy(copy, utf8, length);
    copy[length] = 0;
    JSStringRef s = JSStringCreateWithUTF8CString(copy);
    free(copy);
    return s;
}

// The UTF-8 of s into buffer (size bytes, NUL included); its full length
long copyString(JSStringRef s, char *buffer, long size)
{
    size_t max = JSStringGetMaximumUTF8CStringSize(s);
    char *all = static_cast<char *>(malloc(max));
    if (!all)
        return -1;
    JSStringGetUTF8CString(s, all, max);
    long length = static_cast<long>(strlen(all));
    if (buffer && size > 0) {
        long n = length < size - 1 ? length : size - 1;
        memcpy(buffer, all, n);
        buffer[n] = 0;
    }
    free(all);
    return length;
}

// Remembers what an exception says, as "name:line: message"
void noteException(Task *t, JSValueRef exception)
{
    JSStringRef s = JSValueToStringCopy(t->ctx, exception, nullptr);
    char text[errorSize - 64] = "exception";
    if (s) {
        copyString(s, text, sizeof text);
        JSStringRelease(s);
    }
    char where[64] = "";
    if (JSValueIsObject(t->ctx, exception)) {
        JSObjectRef o = JSValueToObject(t->ctx, exception, nullptr);
        JSStringRef lineName = JSStringCreateWithUTF8CString("line");
        JSStringRef urlName = JSStringCreateWithUTF8CString("sourceURL");
        JSValueRef line = JSObjectGetProperty(t->ctx, o, lineName, nullptr);
        JSValueRef url = JSObjectGetProperty(t->ctx, o, urlName, nullptr);
        JSStringRelease(lineName);
        JSStringRelease(urlName);
        if (JSValueIsNumber(t->ctx, line)) {
            char file[40] = "";
            if (JSValueIsString(t->ctx, url)) {
                JSStringRef u = JSValueToStringCopy(t->ctx, url, nullptr);
                copyString(u, file, sizeof file);
                JSStringRelease(u);
            }
            long n = static_cast<long>(JSValueToNumber(t->ctx, line, nullptr));
            char digits[12];
            int i = sizeof digits - 1;
            digits[i] = 0;
            do {
                digits[--i] = static_cast<char>('0' + n % 10);
                n /= 10;
            } while (n && i > 0);
            strcpy(where, file);
            strcat(where, ":");
            strcat(where, digits + i);
            strcat(where, ": ");
        }
    }
    strcpy(t->error, where);
    strncat(t->error, text, sizeof t->error - strlen(t->error) - 1);
}

// ---- Host classes ----------------------------------------------------------------

Wrapper *wrapperOf(Task *t, JSValueRef v)
{
    if (!v || !JSValueIsObjectOfClass(t->ctx, v, t->wrapperClass))
        return nullptr;
    return static_cast<Wrapper *>(JSObjectGetPrivate(JSValueToObject(t->ctx, v, nullptr)));
}

bool isA(ClassRec *rec, ClassRec *want)
{
    for (; rec; rec = rec->parent) {
        if (rec == want)
            return true;
    }
    return false;
}

// A TypeError, as the web's bindings throw for a call on the wrong object
JSValueRef throwTypeError(Task *t, JSValueRef *exception, const char *message)
{
    JSStringRef s = JSStringCreateWithUTF8CString(message);
    JSValueRef arg = JSValueMakeString(t->ctx, s);
    JSStringRelease(s);
    *exception = JSObjectCallAsConstructor(t->ctx, t->typeError, 1, &arg, nullptr);
    return JSValueMakeUndefined(t->ctx);
}

template<typename T> T fieldAt(const char *p)
{
    T v;
    memcpy(&v, p, sizeof v);
    return v;
}

JSValueRef readField(Task *t, const JSHostSlot *slot, void *object)
{
    const char *p = static_cast<const char *>(object) + slot->js_Offset;
    switch (slot->js_Type) {
    case JSF_LONG: return JSValueMakeNumber(t->ctx, fieldAt<LONG>(p));
    case JSF_ULONG: return JSValueMakeNumber(t->ctx, fieldAt<ULONG>(p));
    case JSF_WORD: return JSValueMakeNumber(t->ctx, fieldAt<WORD>(p));
    case JSF_UWORD: return JSValueMakeNumber(t->ctx, fieldAt<UWORD>(p));
    case JSF_BYTE: return JSValueMakeNumber(t->ctx, *reinterpret_cast<const BYTE *>(p));
    case JSF_UBYTE: return JSValueMakeNumber(t->ctx, *reinterpret_cast<const UBYTE *>(p));
    case JSF_BOOL: return JSValueMakeBoolean(t->ctx, *reinterpret_cast<const UBYTE *>(p) != 0);
    case JSF_STRPTR: {
        const char *s = fieldAt<const char *>(p);
        if (!s)
            return JSValueMakeNull(t->ctx);
        JSStringRef str = JSStringCreateWithUTF8CString(s);
        JSValueRef v = JSValueMakeString(t->ctx, str);
        JSStringRelease(str);
        return v;
    }
    }
    return JSValueMakeUndefined(t->ctx);
}

// The value a slot function gave back: the library owns it now
JSValueRef taken(Task *t, JSValue v)
{
    if (!v)
        return JSValueMakeUndefined(t->ctx);
    JSValueRef r = static_cast<JSValueRef>(v);
    JSValueUnprotect(t->ctx, r);    // still on this stack, so still seen by the collector
    return r;
}

JSValueRef fnCall(JSContextRef, JSObjectRef function, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef *exception)
{
    FnData *f = static_cast<FnData *>(JSObjectGetPrivate(function));
    Task *t = f->task;
    void *object = nullptr;
    if (f->role != RoleFunction) {
        Wrapper *w = wrapperOf(t, self);
        if (!w || !isA(w->rec, f->rec)) {
            const char *className = reinterpret_cast<const char *>(f->rec->host->jc_Name);
            char message[160] = "";
            strncat(message, className, 40);
            strcat(message, ".");
            strncat(message, reinterpret_cast<const char *>(f->slot->js_Name), 60);
            strcat(message, ": not called on a ");
            strncat(message, className, 40);
            return throwTypeError(t, exception, message);
        }
        object = w->object;
    }
    APTR data = f->role == RoleFunction ? f->data : f->rec->host->jc_Data;
    JSValue *args = (JSValue *)(const void *)argv;  // the program's view: read-only by contract
    JSValueRef result = JSValueMakeUndefined(t->ctx);
    switch (f->role) {
    case RoleFunction:
        result = taken(t, f->func(t, nullptr, static_cast<LONG>(argc), args, data));
        break;
    case RoleMethod:
        result = taken(t, reinterpret_cast<JSMethodFunc>(f->slot->js_Func)(t, object, static_cast<LONG>(argc),
            args, data));
        break;
    case RoleGet:
        result = taken(t, reinterpret_cast<JSGetFunc>(f->slot->js_Func)(t, object, data));
        break;
    case RoleSet:
        reinterpret_cast<JSSetFunc>(f->slot->js_Func2)(t, object, argc ? const_cast<OpaqueJSValue *>(argv[0]) : nullptr, data);
        break;
    case RoleField:
        result = readField(t, f->slot, object);
        break;
    }
    jsl_os_fpu_reset();     // the program's code may have opened a library that changed it
    if (t->pending) {
        *exception = t->pending;
        JSValueUnprotect(t->ctx, t->pending);
        t->pending = nullptr;
        return JSValueMakeUndefined(t->ctx);
    }
    return result;
}

void fnFinalize(JSObjectRef function)
{
    delete static_cast<FnData *>(JSObjectGetPrivate(function));
}

void wrapperFinalize(JSObjectRef object)
{
    Wrapper *w = static_cast<Wrapper *>(JSObjectGetPrivate(object));
    if (!w)
        return;
    Task *t = w->task;
    auto it = t->wrappers.find(w->object);
    if (it != t->wrappers.end() && it->second == w) {
        t->wrappers.erase(it);
        if (w->rec->host->jc_Release)
            w->rec->host->jc_Release(t, w->object, w->rec->host->jc_Data);
    }
    if (w->weak)    // NULL once tearDown has let it go
        JSWeakRelease(t->group, w->weak);
    t->allWrappers.erase(w);
    delete w;
}

JSObjectRef makeFn(Task *t, FnData *f)
{
    JSObjectRef fn = JSObjectMake(t->ctx, t->fnClass, f);
    JSObjectSetPrototype(t->ctx, fn, t->functionPrototype);    // call, apply, bind
    return fn;
}

ClassRec *registerClass(Task *t, const JSHostClass *host)
{
    for (ClassRec *r = t->classes; r; r = r->next) {
        if (r->host == host)
            return r;
    }
    ClassRec *parent = host->jc_Parent ? registerClass(t, host->jc_Parent) : nullptr;
    ClassRec *rec = new ClassRec { t->classes, host, parent, JSObjectMake(t->ctx, nullptr, nullptr) };
    JSValueProtect(t->ctx, rec->proto);
    if (parent)
        JSObjectSetPrototype(t->ctx, rec->proto, parent->proto);
    t->classes = rec;
    for (const JSHostSlot *slot = host->jc_Slots; slot && slot->js_Name; slot++) {
        JSStringRef name = JSStringCreateWithUTF8CString(reinterpret_cast<const char *>(slot->js_Name));
        if (slot->js_Kind == JSS_METHOD) {
            JSObjectRef fn = makeFn(t, new FnData { t, RoleMethod, rec, slot, nullptr, nullptr });
            JSObjectSetProperty(t->ctx, rec->proto, name, fn, kJSPropertyAttributeDontEnum, nullptr);
        } else if (slot->js_Kind == JSS_ACCESSOR || slot->js_Kind == JSS_FIELD) {
            Role getRole = slot->js_Kind == JSS_FIELD ? RoleField : RoleGet;
            JSValueRef args[4] = {
                rec->proto,
                JSValueMakeString(t->ctx, name),
                makeFn(t, new FnData { t, getRole, rec, slot, nullptr, nullptr }),
                slot->js_Kind == JSS_ACCESSOR && slot->js_Func2
                    ? static_cast<JSValueRef>(makeFn(t, new FnData { t, RoleSet, rec, slot, nullptr, nullptr }))
                    : JSValueMakeUndefined(t->ctx)
            };
            JSObjectCallAsFunction(t->ctx, t->defineAccessor, nullptr, 4, args, nullptr);
        }
        JSStringRelease(name);
    }
    return rec;
}

// ---- The script thread ------------------------------------------------------------

void setUp(Task *t)
{
    t->group = JSContextGroupCreate();
    t->ctx = JSGlobalContextCreateInGroup(t->group, nullptr);
    JSClassDefinition fn = kJSClassDefinitionEmpty;
    fn.className = "HostFunction";
    fn.callAsFunction = fnCall;
    fn.finalize = fnFinalize;
    t->fnClass = JSClassCreate(&fn);
    JSClassDefinition wrapper = kJSClassDefinitionEmpty;
    wrapper.className = "HostObject";
    wrapper.finalize = wrapperFinalize;
    t->wrapperClass = JSClassCreate(&wrapper);
    JSStringRef src = JSStringCreateWithUTF8CString(
        "(function (o, n, g, s) { Object.defineProperty(o, n, { get: g, set: s, enumerable: true, configurable: true }); })");
    JSValueRef helper = JSEvaluateScript(t->ctx, src, nullptr, nullptr, 1, nullptr);
    JSStringRelease(src);
    t->defineAccessor = JSValueToObject(t->ctx, helper, nullptr);
    JSValueProtect(t->ctx, t->defineAccessor);
    src = JSStringCreateWithUTF8CString("Function.prototype");
    t->functionPrototype = JSValueToObject(t->ctx, JSEvaluateScript(t->ctx, src, nullptr, nullptr, 1, nullptr), nullptr);
    JSStringRelease(src);
    JSValueProtect(t->ctx, t->functionPrototype);
    src = JSStringCreateWithUTF8CString("TypeError");
    t->typeError = JSValueToObject(t->ctx, JSEvaluateScript(t->ctx, src, nullptr, nullptr, 1, nullptr), nullptr);
    JSStringRelease(src);
    JSValueProtect(t->ctx, t->typeError);
}

void tearDown(Task *t)
{
    jsl_cache_commit(&t->cache);
    if (t->pending)
        JSValueUnprotect(t->ctx, t->pending);
    JSValueUnprotect(t->ctx, t->defineAccessor);
    JSValueUnprotect(t->ctx, t->functionPrototype);
    JSValueUnprotect(t->ctx, t->typeError);
    for (ClassRec *r = t->classes; r; r = r->next)
        JSValueUnprotect(t->ctx, r->proto);
    // Weak references go while the VM is whole; the wrappers themselves are
    // finalized (and their release functions told) as the heap goes.
    for (Wrapper *w : t->allWrappers) {
        JSWeakRelease(t->group, w->weak);
        w->weak = nullptr;
    }
    JSGlobalContextRelease(t->ctx);
    JSContextGroupRelease(t->group);
    for (Wrapper *w : t->allWrappers) {     // any the heap did not finalize
        auto it = t->wrappers.find(w->object);
        if (it != t->wrappers.end() && it->second == w && w->rec->host->jc_Release)
            w->rec->host->jc_Release(t, w->object, w->rec->host->jc_Data);
        delete w;
    }
    t->allWrappers.clear();
    t->wrappers.clear();
    JSClassRelease(t->fnClass);
    JSClassRelease(t->wrapperClass);
    while (ClassRec *r = t->classes) {
        t->classes = r->next;
        delete r;
    }
}

void taskMain(void *arg, jsl_box *box)
{
    Task *t = static_cast<Task *>(arg);
    t->box = box;
    setUp(t);
    while (!t->quit) {
        jsl_call *call = jsl_os_receive(box);
        call->fn(call->arg);
        if (call->async)
            jsl_os_free(call);
        else
            jsl_os_reply(box, call);
    }
    tearDown(t);
}

} // namespace

// ---- The calls -------------------------------------------------------------------------

extern "C" {

static Task *newTask(struct TagItem *tags)
{
    Task *t = new Task;
    t->table = engineTable();
    for (struct TagItem *ti = tags; ti && ti->ti_Tag != TAG_DONE; ti++) {
        if (ti->ti_Tag == TAG_MORE) {
            ti = reinterpret_cast<struct TagItem *>(ti->ti_Data);
            if (!ti)
                break;
            ti--;
            continue;
        }
        if (ti->ti_Tag == TAG_SKIP) {
            ti += ti->ti_Data;
            continue;
        }
        switch (ti->ti_Tag) {
        case JSA_StackSize:
            if (ti->ti_Data >= 65536)
                t->stack = ti->ti_Data;
            break;
        case JSA_Name:
            if (ti->ti_Data) {
                strncpy(t->name, reinterpret_cast<const char *>(ti->ti_Data), sizeof t->name - 1);
                t->name[sizeof t->name - 1] = 0;
            }
            break;
        case JSA_UserData:
            t->user = reinterpret_cast<APTR>(ti->ti_Data);
            break;
        case JSA_CacheDir:
            if (ti->ti_Data && strlen(reinterpret_cast<const char *>(ti->ti_Data)) < sizeof t->cacheDir)
                strcpy(t->cacheDir, reinterpret_cast<const char *>(ti->ti_Data));
            break;
        }
    }
    liveTasks++;
    t->box = jsl_os_spawn(t->stack, t->name, taskMain, t);
    if (!t->box) {
        liveTasks--;
        delete t;
        return nullptr;
    }
    return t;
}

static JSTask eNewTask(struct TagItem *tags)
{
    Task *t = nullptr;
    onEngine([&] { t = newTask(tags); });
    return t;
}

static VOID eEndTask(JSTask jt)
{
    Task *t = task(jt);
    if (!t || jsl_os_is_owner(t->box))
        return;     // not from its own script task
    run(t, [&] { t->quit = true; });
    onEngine([&] {
        jsl_os_join(t->box);
        delete t;
        liveTasks--;
    });
}

static LONG eEvaluate(JSTask jt, CONST_STRPTR source, LONG length, CONST_STRPTR name, JSValue *result)
{
    Task *t = task(jt);
    LONG error = JSERR_OK;
    if (result)
        *result = nullptr;
    if (!t || !source)
        return JSERR_BADARGS;
    run(t, [&] {
        JSStringRef src = makeString(reinterpret_cast<const char *>(source), length);
        JSStringRef url = name ? JSStringCreateWithUTF8CString(reinterpret_cast<const char *>(name)) : nullptr;
        if (!src) {
            error = JSERR_NOMEMORY;
            return;
        }
        JSValueRef exception = nullptr;
        JSValueRef v = jsl_cache_evaluate(t->ctx, src, url, t->cacheDir, &t->cache, &exception);
        JSStringRelease(src);
        if (url)
            JSStringRelease(url);
        if (exception) {
            noteException(t, exception);
            error = JSERR_EXCEPTION;
            v = exception;
        }
        if (result)
            *result = out(t, v);
    });
    return error;
}

struct PostJob {
    jsl_call call;
    Task *task;
    JSJobFunc func;
    APTR data;
};

static void runPostJob(void *arg)
{
    PostJob *job = static_cast<PostJob *>(arg);
    job->func(job->task, job->data);
}

static LONG ePost(JSTask jt, JSJobFunc func, APTR data, ULONG wait)
{
    Task *t = task(jt);
    LONG result = 0;
    if (!t || !func)
        return JSERR_BADARGS;
    if (wait) {
        run(t, [&] { result = func(t, data); });
        return result;
    }
    // Queued, the job's memory is freed by the script thread (taskMain)
    PostJob *job = static_cast<PostJob *>(jsl_os_alloc(sizeof(PostJob)));
    if (!job)
        return JSERR_NOMEMORY;
    static_assert(offsetof(PostJob, call) == 0, "the call is first: taskMain frees it");
    job->call.fn = runPostJob;
    job->call.arg = job;
    job->call.async = 1;
    job->task = t;
    job->func = func;
    job->data = data;
    jsl_os_post(t->box, &job->call);
    return JSERR_OK;
}

static APTR eUserData(JSTask jt)
{
    return jt ? task(jt)->user : nullptr;
}

static JSValue eGlobal(JSTask jt)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] { r = out(t, JSContextGetGlobalObject(t->ctx)); });
    return r;
}

static VOID eFree(JSTask jt, JSValue v)
{
    Task *t = task(jt);
    if (v)
        run(t, [&] { JSValueUnprotect(t->ctx, static_cast<JSValueRef>(v)); });
}

static JSValue eDup(JSTask jt, JSValue v)
{
    Task *t = task(jt);
    if (v)
        run(t, [&] { JSValueProtect(t->ctx, static_cast<JSValueRef>(v)); });
    return v;
}

static LONG eTypeOf(JSTask jt, JSValue v)
{
    Task *t = task(jt);
    LONG r = JST_UNDEFINED;
    if (!v)
        return r;
    run(t, [&] {
        JSValueRef value = static_cast<JSValueRef>(v);
        switch (JSValueGetType(t->ctx, value)) {
        case kJSTypeUndefined: r = JST_UNDEFINED; break;
        case kJSTypeNull: r = JST_NULL; break;
        case kJSTypeBoolean: r = JST_BOOLEAN; break;
        case kJSTypeNumber: r = JST_NUMBER; break;
        case kJSTypeString: r = JST_STRING; break;
        case kJSTypeSymbol: r = JST_SYMBOL; break;
        case kJSTypeBigInt: r = JST_BIGINT; break;
        case kJSTypeObject:
            r = JSObjectIsFunction(t->ctx, JSValueToObject(t->ctx, value, nullptr)) ? JST_FUNCTION : JST_OBJECT;
            break;
        }
    });
    return r;
}

static JSValue eNewLong(JSTask jt, LONG n)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] { r = out(t, JSValueMakeNumber(t->ctx, n)); });
    return r;
}

static JSValue eNewDouble(JSTask jt, CONST DOUBLE *n)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    if (!n)
        return nullptr;
    run(t, [&] { r = out(t, JSValueMakeNumber(t->ctx, *n)); });
    return r;
}

static JSValue eNewBool(JSTask jt, LONG b)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] { r = out(t, JSValueMakeBoolean(t->ctx, b != 0)); });
    return r;
}

static JSValue eNewNull(JSTask jt)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] { r = out(t, JSValueMakeNull(t->ctx)); });
    return r;
}

static JSValue eNewString(JSTask jt, CONST_STRPTR utf8, LONG length)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] {
        JSStringRef s = makeString(reinterpret_cast<const char *>(utf8), length);
        if (!s)
            return;
        r = out(t, JSValueMakeString(t->ctx, s));
        JSStringRelease(s);
    });
    return r;
}

static JSValue eNewObject(JSTask jt)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] { r = out(t, JSObjectMake(t->ctx, nullptr, nullptr)); });
    return r;
}

static JSValue eNewFunction(JSTask jt, CONST_STRPTR name, JSMethodFunc func, APTR data)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    (void)name;
    if (!func)
        return nullptr;
    run(t, [&] { r = out(t, makeFn(t, new FnData { t, RoleFunction, nullptr, nullptr, func, data })); });
    return r;
}

static LONG eToLong(JSTask jt, JSValue v, LONG *result)
{
    Task *t = task(jt);
    LONG error = JSERR_OK;
    run(t, [&] {
        JSValueRef exception = nullptr;
        double d = JSValueToNumber(t->ctx, in(t, v), &exception);
        if (exception) {
            noteException(t, exception);
            error = JSERR_EXCEPTION;
            return;
        }
        // as JavaScript's ToInt32
        if (d != d || d == __builtin_inf() || d == -__builtin_inf()) {
            *result = 0;
            return;
        }
        double m = __builtin_fmod(__builtin_trunc(d), 4294967296.0);
        if (m < 0)
            m += 4294967296.0;
        *result = static_cast<LONG>(static_cast<ULONG>(m));
    });
    return error;
}

static LONG eToDouble(JSTask jt, JSValue v, DOUBLE *result)
{
    Task *t = task(jt);
    LONG error = JSERR_OK;
    run(t, [&] {
        JSValueRef exception = nullptr;
        double d = JSValueToNumber(t->ctx, in(t, v), &exception);
        if (exception) {
            noteException(t, exception);
            error = JSERR_EXCEPTION;
            return;
        }
        *result = d;
    });
    return error;
}

static LONG eToBool(JSTask jt, JSValue v)
{
    Task *t = task(jt);
    LONG r = 0;
    run(t, [&] { r = JSValueToBoolean(t->ctx, in(t, v)) ? 1 : 0; });
    return r;
}

static LONG eToString(JSTask jt, JSValue v, STRPTR buffer, LONG size)
{
    Task *t = task(jt);
    LONG length = -1;
    run(t, [&] {
        JSValueRef exception = nullptr;
        JSStringRef s = JSValueToStringCopy(t->ctx, in(t, v), &exception);
        if (exception) {
            noteException(t, exception);
            return;
        }
        length = copyString(s, reinterpret_cast<char *>(buffer), size);
        JSStringRelease(s);
    });
    return length;
}

static JSObjectRef objectFor(Task *t, JSValue v, JSValueRef *exception)
{
    if (!v)
        return nullptr;
    return JSValueToObject(t->ctx, static_cast<JSValueRef>(v), exception);
}

static JSValue eGetProp(JSTask jt, JSValue object, CONST_STRPTR name)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] {
        JSValueRef exception = nullptr;
        JSObjectRef o = objectFor(t, object, &exception);
        if (!o || !name) {
            if (exception)
                noteException(t, exception);
            return;
        }
        JSStringRef n = JSStringCreateWithUTF8CString(reinterpret_cast<const char *>(name));
        JSValueRef v = JSObjectGetProperty(t->ctx, o, n, &exception);
        JSStringRelease(n);
        if (exception) {
            noteException(t, exception);
            return;
        }
        r = out(t, v);
    });
    return r;
}

static LONG eSetProp(JSTask jt, JSValue object, CONST_STRPTR name, JSValue value)
{
    Task *t = task(jt);
    LONG error = JSERR_OK;
    run(t, [&] {
        JSValueRef exception = nullptr;
        JSObjectRef o = objectFor(t, object, &exception);
        if (!o || !name) {
            error = JSERR_TYPE;
            return;
        }
        JSStringRef n = JSStringCreateWithUTF8CString(reinterpret_cast<const char *>(name));
        JSObjectSetProperty(t->ctx, o, n, in(t, value), kJSPropertyAttributeNone, &exception);
        JSStringRelease(n);
        if (exception) {
            noteException(t, exception);
            error = JSERR_EXCEPTION;
        }
    });
    return error;
}

static JSValue eGetIndex(JSTask jt, JSValue object, ULONG index)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    run(t, [&] {
        JSValueRef exception = nullptr;
        JSObjectRef o = objectFor(t, object, &exception);
        if (!o)
            return;
        JSValueRef v = JSObjectGetPropertyAtIndex(t->ctx, o, index, &exception);
        if (exception) {
            noteException(t, exception);
            return;
        }
        r = out(t, v);
    });
    return r;
}

static LONG eSetIndex(JSTask jt, JSValue object, ULONG index, JSValue value)
{
    Task *t = task(jt);
    LONG error = JSERR_OK;
    run(t, [&] {
        JSValueRef exception = nullptr;
        JSObjectRef o = objectFor(t, object, &exception);
        if (!o) {
            error = JSERR_TYPE;
            return;
        }
        JSObjectSetPropertyAtIndex(t->ctx, o, index, in(t, value), &exception);
        if (exception) {
            noteException(t, exception);
            error = JSERR_EXCEPTION;
        }
    });
    return error;
}

static LONG eCall(JSTask jt, JSValue func, JSValue self, LONG argc, JSValue *argv, JSValue *result)
{
    Task *t = task(jt);
    LONG error = JSERR_OK;
    if (result)
        *result = nullptr;
    run(t, [&] {
        JSObjectRef f = func ? JSValueToObject(t->ctx, static_cast<JSValueRef>(func), nullptr) : nullptr;
        if (!f || !JSObjectIsFunction(t->ctx, f)) {
            error = JSERR_TYPE;
            return;
        }
        JSObjectRef thisObject = self && JSValueIsObject(t->ctx, static_cast<JSValueRef>(self))
            ? JSValueToObject(t->ctx, static_cast<JSValueRef>(self), nullptr) : nullptr;
        JSValueRef small[8];
        JSValueRef *args = argc <= 8 ? small : static_cast<JSValueRef *>(malloc(sizeof(JSValueRef) * argc));
        if (!args) {
            error = JSERR_NOMEMORY;
            return;
        }
        for (LONG i = 0; i < argc; i++)
            args[i] = in(t, argv[i]);
        JSValueRef exception = nullptr;
        JSValueRef v = JSObjectCallAsFunction(t->ctx, f, thisObject, argc, args, &exception);
        if (args != small)
            free(args);
        if (exception) {
            noteException(t, exception);
            error = JSERR_EXCEPTION;
            v = exception;
        }
        if (result)
            *result = out(t, v);
    });
    return error;
}

static JSClass eRegisterClass(JSTask jt, CONST struct JSHostClass *host)
{
    Task *t = task(jt);
    JSClass r = nullptr;
    if (!host || !host->jc_Name)
        return nullptr;
    run(t, [&] { r = registerClass(t, host); });
    return r;
}

static JSValue eWrap(JSTask jt, JSClass cls, APTR object)
{
    Task *t = task(jt);
    JSValue r = nullptr;
    if (!cls)
        return nullptr;
    run(t, [&] {
        if (!object) {
            r = out(t, JSValueMakeNull(t->ctx));
            return;
        }
        auto it = t->wrappers.find(object);
        if (it != t->wrappers.end()) {
            JSObjectRef live = JSWeakGetObject(it->second->weak);
            if (live) {
                r = out(t, live);
                return;
            }
        }
        ClassRec *rec = static_cast<ClassRec *>(cls);
        Wrapper *w = new Wrapper { t, rec, object, nullptr };
        JSObjectRef o = JSObjectMake(t->ctx, t->wrapperClass, w);
        JSObjectSetPrototype(t->ctx, o, rec->proto);
        w->weak = JSWeakCreate(t->group, o);
        t->allWrappers.insert(w);
        t->wrappers[object] = w;    // an older, dead wrapper's finalize leaves this one alone
        r = out(t, o);
    });
    return r;
}

static APTR eUnwrap(JSTask jt, JSValue v, JSClass cls)
{
    Task *t = task(jt);
    APTR r = nullptr;
    if (!v)
        return nullptr;
    run(t, [&] {
        Wrapper *w = wrapperOf(t, static_cast<JSValueRef>(v));
        if (w && (!cls || isA(w->rec, static_cast<ClassRec *>(cls))))
            r = w->object;
    });
    return r;
}

static JSValue eThrow(JSTask jt, CONST_STRPTR message)
{
    Task *t = task(jt);
    run(t, [&] {
        JSStringRef s = JSStringCreateWithUTF8CString(message ? reinterpret_cast<const char *>(message) : "error");
        JSValueRef arg = JSValueMakeString(t->ctx, s);
        JSStringRelease(s);
        JSValueRef error = JSObjectMakeError(t->ctx, 1, &arg, nullptr);
        if (t->pending)
            JSValueUnprotect(t->ctx, t->pending);
        JSValueProtect(t->ctx, error);
        t->pending = error;
    });
    return nullptr;
}

static LONG eErrorText(JSTask jt, STRPTR buffer, LONG size)
{
    Task *t = task(jt);
    LONG length = 0;
    run(t, [&] {
        length = static_cast<LONG>(strlen(t->error));
        if (buffer && size > 0) {
            LONG n = length < size - 1 ? length : size - 1;
            memcpy(buffer, t->error, n);
            buffer[n] = 0;
        }
    });
    return length;
}

static const JSEngineTable table = {
    JSL_TABLE_VERSION, sizeof(JSEngineTable),
    eNewTask, eEndTask, eEvaluate, ePost, eUserData, eGlobal, eFree, eDup, eTypeOf,
    eNewLong, eNewDouble, eNewBool, eNewNull, eNewString, eNewObject, eNewFunction,
    eToLong, eToDouble, eToBool, eToString, eGetProp, eSetProp, eGetIndex, eSetIndex,
    eCall, eRegisterClass, eWrap, eUnwrap, eThrow, eErrorText
};

CONST struct JSEngineTable *jsl_engine_table(void)
{
    return &table;
}

LONG jsl_engine_live_tasks(void)
{
    return liveTasks.load();
}

} // extern "C"

namespace {
const JSEngineTable *engineTable() { return &table; }
}
