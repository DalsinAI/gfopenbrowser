/* unicode.library, our own code: Unicode properties.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * ICU's C calls for character properties, by their own interface (ICU's
 * headers give the types and the enum numbers), answered from the tables
 * gen_props.py makes from the UCD at build time (uo_data.c). The table
 * entries of calls.txt that own.txt names point here (uo_<call>).
 *
 * A property or a value the browser and JavaScriptCore never ask for goes
 * to ICU's own function for now (the calls marked "ICU" below), and leaves
 * with it.
 */
#include <string.h>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utypes.h>
#include <unicode/uversion.h>

#include "uo_data.h"
#include "uo_own.h"

#define MASK(gc) (1u << (gc))
#define GC_L (MASK(U_UPPERCASE_LETTER) | MASK(U_LOWERCASE_LETTER) | MASK(U_TITLECASE_LETTER) | \
              MASK(U_MODIFIER_LETTER) | MASK(U_OTHER_LETTER))
#define GC_P (MASK(U_DASH_PUNCTUATION) | MASK(U_START_PUNCTUATION) | MASK(U_END_PUNCTUATION) | \
              MASK(U_CONNECTOR_PUNCTUATION) | MASK(U_OTHER_PUNCTUATION) | \
              MASK(U_INITIAL_PUNCTUATION) | MASK(U_FINAL_PUNCTUATION))
#define GC_C (MASK(U_UNASSIGNED) | MASK(U_CONTROL_CHAR) | MASK(U_FORMAT_CHAR) | \
              MASK(U_PRIVATE_USE_CHAR) | MASK(U_SURROGATE))
#define GC_Z (MASK(U_SPACE_SEPARATOR) | MASK(U_LINE_SEPARATOR) | MASK(U_PARAGRAPH_SEPARATOR))

/* Out of range: ICU's tries give the values of an unassigned code point. */
#define IN_RANGE(c) ((uint32_t)(c) <= 0x10FFFF)

const struct uo_prop *uo_prop_of(UChar32 c)
{
    return &uo_props[IN_RANGE(c) ? UO_PROPS(c) : UO_PROPS(0x10FFFF)];
}

static unsigned gcOf(UChar32 c)
{
    return uo_prop_of(c)->gc;
}

int uo_has_binary(UChar32 c, int bit)
{
    const uint32_t *set;

    if (!IN_RANGE(c) || bit < 0)
        return 0;
    set = uo_bin_sets[UO_BIN(c)];
    return (set[bit >> 5] >> (bit & 31)) & 1;
}

int8_t uo_u_charType(UChar32 c)
{
    return (int8_t)gcOf(c);
}

UCharDirection uo_u_charDirection(UChar32 c)
{
    if (!IN_RANGE(c))
        return U_LEFT_TO_RIGHT;              /* as ICU's error value */
    return (UCharDirection)uo_prop_of(c)->bc;
}

uint8_t uo_u_getCombiningClass(UChar32 c)
{
    return uo_prop_of(c)->ccc;
}

UChar32 uo_u_charMirror(UChar32 c)
{
    int lo = 0, hi = UO_MIRROR_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (uo_mirror[mid][0] == c)
            return uo_mirror[mid][1];
        if (uo_mirror[mid][0] < c)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return c;
}

/* ICU's bidi properties (ubidi_props), on which ICU's own bidi
   algorithm, IDNA and properties code call */
static const struct uo_range *joiningOf(UChar32 c)
{
    int lo = 0, hi = UO_JOINING_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (c < uo_joining[mid].first)
            hi = mid - 1;
        else if (c > uo_joining[mid].last)
            lo = mid + 1;
        else
            return &uo_joining[mid];
    }
    return NULL;
}

UJoiningType uo_ubidi_getJoiningType(UChar32 c)
{
    const struct uo_range *r = joiningOf(c);
    return r ? (UJoiningType)(r->value & 15) : U_JT_NON_JOINING;
}

UJoiningGroup uo_ubidi_getJoiningGroup(UChar32 c)
{
    const struct uo_range *r = joiningOf(c);
    return r ? (UJoiningGroup)(r->value >> 4) : U_JG_NO_JOINING_GROUP;
}

static const int32_t *bracketOf(UChar32 c)
{
    int lo = 0, hi = UO_BRACKET_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (uo_brackets[mid][0] == c)
            return uo_brackets[mid];
        if (uo_brackets[mid][0] < c)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

UBidiPairedBracketType uo_ubidi_getPairedBracketType(UChar32 c)
{
    const int32_t *b = bracketOf(c);
    return b ? (UBidiPairedBracketType)b[2] : U_BPT_NONE;
}

UChar32 uo_u_getBidiPairedBracket(UChar32 c)
{
    const int32_t *b = bracketOf(c);
    return b ? b[1] : c;
}

int32_t uo_ubidi_getMaxValue(UProperty which)
{
    switch (which) {
    case UCHAR_BIDI_CLASS:
        return UO_MAX_BC;
    case UCHAR_JOINING_GROUP:
        return UO_MAX_JG;
    case UCHAR_JOINING_TYPE:
        return UO_MAX_JT;
    case UCHAR_BIDI_PAIRED_BRACKET_TYPE:
        return UO_MAX_BPT;
    default:
        return -1;
    }
}

UBool uo_u_isalpha(UChar32 c)
{
    return (MASK(gcOf(c)) & GC_L) != 0;
}

UBool uo_u_isalnum(UChar32 c)
{
    return (MASK(gcOf(c)) & (GC_L | MASK(U_DECIMAL_DIGIT_NUMBER))) != 0;
}

UBool uo_u_isdigit(UChar32 c)
{
    return gcOf(c) == U_DECIMAL_DIGIT_NUMBER;
}

UBool uo_u_ispunct(UChar32 c)
{
    return (MASK(gcOf(c)) & GC_P) != 0;
}

UBool uo_u_isprint(UChar32 c)
{
    return (MASK(gcOf(c)) & GC_C) == 0;
}

static int bitOf(UProperty which)
{
    return (unsigned)which < UCHAR_BINARY_LIMIT ? uo_bin_bit[which] : -1;
}

UBool uo_u_isUWhiteSpace(UChar32 c)
{
    return uo_has_binary(c, uo_bin_bit[UCHAR_WHITE_SPACE]);
}

/* ICU's POSIX-style properties (uprops.cpp), from the general category */
static int posix(UChar32 c, UProperty which)
{
    unsigned m = MASK(gcOf(c));

    switch (which) {
    case UCHAR_POSIX_ALNUM:
        return uo_has_binary(c, uo_bin_bit[UCHAR_ALPHABETIC]) || m == MASK(U_DECIMAL_DIGIT_NUMBER);
    case UCHAR_POSIX_BLANK:
        if (c <= 0x9F)
            return c == 9 || c == 0x20;
        return m == MASK(U_SPACE_SEPARATOR);
    case UCHAR_POSIX_GRAPH:
        return (m & (MASK(U_CONTROL_CHAR) | MASK(U_SURROGATE) | MASK(U_UNASSIGNED) | GC_Z)) == 0;
    case UCHAR_POSIX_PRINT:
        return m == MASK(U_SPACE_SEPARATOR) ||
               (m & (MASK(U_CONTROL_CHAR) | MASK(U_SURROGATE) | MASK(U_UNASSIGNED) | GC_Z)) == 0;
    case UCHAR_POSIX_XDIGIT:
        if ((c >= 0x41 && c <= 0x46) || (c >= 0x61 && c <= 0x66) ||
            (c >= 0xFF21 && c <= 0xFF26) || (c >= 0xFF41 && c <= 0xFF46))
            return 1;
        return m == MASK(U_DECIMAL_DIGIT_NUMBER);
    default:
        return -1;
    }
}

UBool uo_u_hasBinaryProperty(UChar32 c, UProperty which)
{
    int bit = bitOf(which), p;

    if (c < 0 && (which == UCHAR_CHANGES_WHEN_LOWERCASED || which == UCHAR_CHANGES_WHEN_UPPERCASED ||
                  which == UCHAR_CHANGES_WHEN_TITLECASED || which == UCHAR_CHANGES_WHEN_CASEMAPPED))
        return 1;                            /* ICU says so for negative values */
    if (bit >= 0)
        return uo_has_binary(c, bit);
    if ((p = posix(c, which)) >= 0)
        return (UBool)p;
    return u_hasBinaryProperty(c, which);      /* ICU */
}

UBlockCode uo_ublock_getCode(UChar32 c)
{
    /* the last block found first: text stays in one block for a while */
    static int last;
    int lo = 0, hi = UO_BLOCK_COUNT - 1, i = last;

    if (c >= uo_blocks[i].first && c <= uo_blocks[i].last)
        return (UBlockCode)uo_blocks[i].value;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (c < uo_blocks[mid].first)
            hi = mid - 1;
        else if (c > uo_blocks[mid].last)
            lo = mid + 1;
        else {
            last = mid;
            return (UBlockCode)uo_blocks[mid].value;
        }
    }
    return UBLOCK_NO_BLOCK;
}

UScriptCode uo_uscript_getScript(UChar32 c, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return USCRIPT_INVALID_CODE;
    if (!IN_RANGE(c)) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return USCRIPT_INVALID_CODE;
    }
    return (UScriptCode)uo_prop_of(c)->sc;
}

static const uint8_t *scxOf(UChar32 c)
{
    int lo = 0, hi = UO_SCX_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (c < uo_scx[mid].first)
            hi = mid - 1;
        else if (c > uo_scx[mid].last)
            lo = mid + 1;
        else
            return &uo_scx_sets[uo_scx[mid].value];
    }
    return 0;
}

UBool uo_uscript_hasScript(UChar32 c, UScriptCode sc)
{
    const uint8_t *set = IN_RANGE(c) ? scxOf(c) : 0;
    int i;

    if (!set)
        return sc == (UScriptCode)uo_prop_of(c)->sc;
    for (i = 1; i <= set[0]; i++)
        if (set[i] == sc)
            return 1;
    return 0;
}

const char *uo_uscript_getShortName(UScriptCode sc)
{
    if ((unsigned)sc < UO_SCRIPT_COUNT && uo_script_short[sc])
        return uo_script_short[sc];
    return uscript_getShortName(sc);          /* ICU: codes with no characters */
}

UBool uo_uscript_isRightToLeft(UScriptCode sc)
{
    return (unsigned)sc < UO_SCRIPT_COUNT && uo_script_rtl[sc];
}

/* ICU's loose match of property names: case and '-', '_', ' ' do not count */
static int looseCompare(const char *a, const char *b)
{
    for (;;) {
        unsigned char x, y;
        while (*a == '-' || *a == '_' || *a == ' ' || (*a >= 9 && *a <= 13))
            a++;
        while (*b == '-' || *b == '_' || *b == ' ' || (*b >= 9 && *b <= 13))
            b++;
        x = (unsigned char)*a;
        y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y || !x)
            return (int)x - (int)y;
        a++;
        b++;
    }
}

int32_t uo_u_getPropertyValueEnum(UProperty property, const char *alias)
{
    if (property == UCHAR_SCRIPT && alias) {
        int lo = 0, hi = UO_SCNAME_COUNT - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1, d = looseCompare(alias, uo_script_names[mid].name);
            if (!d)
                return uo_script_names[mid].sc;
            if (d < 0)
                hi = mid - 1;
            else
                lo = mid + 1;
        }
        return UCHAR_INVALID_CODE;
    }
    return u_getPropertyValueEnum(property, alias);   /* ICU */
}

int32_t uo_u_getIntPropertyValue(UChar32 c, UProperty which)
{
    const struct uo_prop *p;

    if ((unsigned)which < UCHAR_BINARY_LIMIT)
        return uo_u_hasBinaryProperty(c, which);
    p = uo_prop_of(c);
    switch (which) {
    case UCHAR_BIDI_CLASS:
        return uo_u_charDirection(c);
    case UCHAR_BLOCK:
        return uo_ublock_getCode(c);
    case UCHAR_CANONICAL_COMBINING_CLASS:
        return p->ccc;
    case UCHAR_EAST_ASIAN_WIDTH:
        return p->ea_nt_vo & 7;
    case UCHAR_GENERAL_CATEGORY:
        return p->gc;
    case UCHAR_LINE_BREAK:
        return p->lb;
    case UCHAR_NUMERIC_TYPE:
        return (p->ea_nt_vo >> 3) & 3;
    case UCHAR_SCRIPT:
        return IN_RANGE(c) ? p->sc : USCRIPT_INVALID_CODE;
    case UCHAR_VERTICAL_ORIENTATION:
        return (p->ea_nt_vo >> 5) & 3;
    case UCHAR_GENERAL_CATEGORY_MASK:
        return (int32_t)MASK(p->gc);
    case UCHAR_JOINING_TYPE:
        return uo_ubidi_getJoiningType(c);
    case UCHAR_JOINING_GROUP:
        return uo_ubidi_getJoiningGroup(c);
    case UCHAR_BIDI_PAIRED_BRACKET_TYPE:
        return uo_ubidi_getPairedBracketType(c);
    default:
        return u_getIntPropertyValue(c, which);   /* ICU */
    }
}

void uo_u_getVersion(UVersionInfo info)
{
    /* the interface's version: ICU 78.3, whose calls these are */
    if (info) {
        memset(info, 0, U_MAX_VERSION_LENGTH);
        info[0] = U_ICU_VERSION_MAJOR_NUM;
        info[1] = U_ICU_VERSION_MINOR_NUM;
        info[2] = U_ICU_VERSION_PATCHLEVEL_NUM;
    }
}

const char *uo_u_errorName(UErrorCode code)
{
    int lo = 0, hi = UO_ERR_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (uo_errors[mid].code == code)
            return uo_errors[mid].name;
        if (uo_errors[mid].code < code)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return "[BOGUS UErrorCode]";
}

int32_t uo_u_countChar32(const UChar *s, int32_t length)
{
    int32_t n = 0, i;

    if (!s || length < -1)
        return 0;
    if (length == -1) {
        for (i = 0; s[i]; i++, n++)
            if (U16_IS_LEAD(s[i]) && U16_IS_TRAIL(s[i + 1]))
                i++;
        return n;
    }
    for (i = 0; i < length; i++, n++)
        if (U16_IS_LEAD(s[i]) && i + 1 < length && U16_IS_TRAIL(s[i + 1]))
            i++;
    return n;
}
