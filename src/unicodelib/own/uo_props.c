/* unicode.library, our own code: Unicode properties.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * ICU's C calls for character properties, by their own interface (ICU's
 * headers give the types and the enum numbers), answered from the tables
 * gen_props.py makes from the UCD at build time (uo_data.c). The table
 * entries of calls.txt that own.txt names point here (uo_<call>).
 *
 * Every property ICU 78 has is ours, so uo_icu.c can answer ICU's own
 * uchar.cpp and uprops.cpp calls with these and those files leave the
 * library.
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

static const struct uo_more *moreOf(UChar32 c)
{
    return &uo_more[IN_RANGE(c) ? UO_MORE(c) : UO_MORE(0x10FFFF)];
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
    if (!IN_RANGE(c))       /* ICU's normalization data: all inert out there */
        return which >= UCHAR_NFD_INERT && which <= UCHAR_SEGMENT_STARTER;
    switch (which) {
    case UCHAR_NFD_INERT:
        return (moreOf(c)->flags & UO_NFD_INERT) != 0;
    case UCHAR_NFKD_INERT:
        return (moreOf(c)->flags & UO_NFKD_INERT) != 0;
    case UCHAR_NFC_INERT:
        return (moreOf(c)->flags & UO_NFC_INERT) != 0;
    case UCHAR_NFKC_INERT:
        return (moreOf(c)->flags & UO_NFKC_INERT) != 0;
    case UCHAR_SEGMENT_STARTER:
        return (moreOf(c)->flags & UO_SEGMENT_STARTER) != 0;
    case UCHAR_BASIC_EMOJI:
    case UCHAR_RGI_EMOJI:
        return (moreOf(c)->flags & UO_BASIC_EMOJI) != 0;
    case UCHAR_CASE_SENSITIVE:
        return (moreOf(c)->flags & UO_CASE_SENSITIVE) != 0;
    default:
        return 0;       /* the other emoji sets are strings, never one code point */
    }
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

/* lead and trail combining classes (ICU's FCD values) */
static int fcdOf(UChar32 c, int trail)
{
    int lo = 0, hi = UO_FCD_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        UChar32 m = (UChar32)(uo_fcd_lead[mid] >> 8);
        if (m == c)
            return trail ? uo_fcd_trail[mid] : (int)(uo_fcd_lead[mid] & 0xFF);
        if (m < c)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return uo_prop_of(c)->ccc;
}

/* ICU: Hangul_Syllable_Type from Grapheme_Cluster_Break, BMP only */
static int hstOf(UChar32 c)
{
    if (!IN_RANGE(c) || c > 0xFFFF)
        return U_HST_NOT_APPLICABLE;
    switch (moreOf(c)->gcb) {
    case U_GCB_L:
        return U_HST_LEADING_JAMO;
    case U_GCB_V:
        return U_HST_VOWEL_JAMO;
    case U_GCB_T:
        return U_HST_TRAILING_JAMO;
    case U_GCB_LV:
        return U_HST_LV_SYLLABLE;
    case U_GCB_LVT:
        return U_HST_LVT_SYLLABLE;
    default:
        return U_HST_NOT_APPLICABLE;
    }
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
    case UCHAR_DECOMPOSITION_TYPE:
        return moreOf(c)->dt;
    case UCHAR_EAST_ASIAN_WIDTH:
        return p->ea_nt_vo & 7;
    case UCHAR_GENERAL_CATEGORY:
        return p->gc;
    case UCHAR_JOINING_GROUP:
        return uo_ubidi_getJoiningGroup(c);
    case UCHAR_JOINING_TYPE:
        return uo_ubidi_getJoiningType(c);
    case UCHAR_LINE_BREAK:
        return p->lb;
    case UCHAR_NUMERIC_TYPE:
        return (p->ea_nt_vo >> 3) & 3;
    case UCHAR_SCRIPT:
        return IN_RANGE(c) ? p->sc : USCRIPT_INVALID_CODE;
    case UCHAR_HANGUL_SYLLABLE_TYPE:
        return hstOf(c);
    case UCHAR_NFD_QUICK_CHECK:
        return moreOf(c)->qc & 1;
    case UCHAR_NFKD_QUICK_CHECK:
        return (moreOf(c)->qc >> 1) & 1;
    case UCHAR_NFC_QUICK_CHECK:
        return (moreOf(c)->qc >> 2) & 3;
    case UCHAR_NFKC_QUICK_CHECK:
        return (moreOf(c)->qc >> 4) & 3;
    case UCHAR_LEAD_CANONICAL_COMBINING_CLASS:
        return fcdOf(c, 0);
    case UCHAR_TRAIL_CANONICAL_COMBINING_CLASS:
        return fcdOf(c, 1);
    case UCHAR_GRAPHEME_CLUSTER_BREAK:
        return moreOf(c)->gcb;
    case UCHAR_SENTENCE_BREAK:
        return moreOf(c)->sb;
    case UCHAR_WORD_BREAK:
        return moreOf(c)->wb;
    case UCHAR_BIDI_PAIRED_BRACKET_TYPE:
        return uo_ubidi_getPairedBracketType(c);
    case UCHAR_INDIC_POSITIONAL_CATEGORY:
        return moreOf(c)->inpc;
    case UCHAR_INDIC_SYLLABIC_CATEGORY:
        return moreOf(c)->insc;
    case UCHAR_VERTICAL_ORIENTATION:
        return (p->ea_nt_vo >> 5) & 3;
    case UCHAR_IDENTIFIER_STATUS:
        return (uo_idtypes[moreOf(c)->idt] & (1u << U_ID_TYPE_INCLUSION | 1u << U_ID_TYPE_RECOMMENDED)) ?
               U_ID_STATUS_ALLOWED : U_ID_STATUS_RESTRICTED;
    case UCHAR_INDIC_CONJUNCT_BREAK:
        return moreOf(c)->incb;
    case UCHAR_GENERAL_CATEGORY_MASK:
        return (int32_t)MASK(p->gc);
    default:
        return 0;       /* not an integer property */
    }
}

int32_t uo_u_getIntPropertyMaxValue(UProperty which)
{
    if ((unsigned)which < UCHAR_BINARY_LIMIT)
        return 1;
    switch (which) {
    case UCHAR_BIDI_CLASS:
    case UCHAR_JOINING_GROUP:
    case UCHAR_JOINING_TYPE:
    case UCHAR_BIDI_PAIRED_BRACKET_TYPE:
        return uo_ubidi_getMaxValue(which);
    case UCHAR_BLOCK:
        return UO_MAX_BLOCK;
    case UCHAR_CANONICAL_COMBINING_CLASS:
    case UCHAR_LEAD_CANONICAL_COMBINING_CLASS:
    case UCHAR_TRAIL_CANONICAL_COMBINING_CLASS:
        return 0xFF;
    case UCHAR_DECOMPOSITION_TYPE:
        return UO_MAX_DT;
    case UCHAR_EAST_ASIAN_WIDTH:
        return UO_MAX_EA;
    case UCHAR_GENERAL_CATEGORY:
        return U_CHAR_CATEGORY_COUNT - 1;
    case UCHAR_LINE_BREAK:
        return UO_MAX_LB;
    case UCHAR_NUMERIC_TYPE:
        return U_NT_COUNT - 1;
    case UCHAR_SCRIPT:
        return UO_MAX_SC;
    case UCHAR_HANGUL_SYLLABLE_TYPE:
        return U_HST_COUNT - 1;
    case UCHAR_NFD_QUICK_CHECK:
    case UCHAR_NFKD_QUICK_CHECK:
        return 1;       /* UNORM_YES: never "maybe" */
    case UCHAR_NFC_QUICK_CHECK:
    case UCHAR_NFKC_QUICK_CHECK:
        return 2;       /* UNORM_MAYBE */
    case UCHAR_GRAPHEME_CLUSTER_BREAK:
        return UO_MAX_GCB;
    case UCHAR_SENTENCE_BREAK:
        return UO_MAX_SB;
    case UCHAR_WORD_BREAK:
        return UO_MAX_WB;
    case UCHAR_INDIC_POSITIONAL_CATEGORY:
        return UO_MAX_INPC;
    case UCHAR_INDIC_SYLLABIC_CATEGORY:
        return UO_MAX_INSC;
    case UCHAR_VERTICAL_ORIENTATION:
        return UO_MAX_VO;
    case UCHAR_IDENTIFIER_STATUS:
        return U_ID_STATUS_ALLOWED;
    case UCHAR_INDIC_CONJUNCT_BREAK:
        return 0;       /* as ICU 78 says (its table has no maximum for it) */
    default:
        return -1;
    }
}

void uo_u_charAge(UChar32 c, UVersionInfo info)
{
    int lo = 0, hi = UO_AGE_RUNS - 1, a = 0;

    if (!info)
        return;
    if (IN_RANGE(c)) {
        while (lo <= hi) {          /* the last run starting at or before c */
            int mid = (lo + hi) >> 1;
            if ((UChar32)(uo_age_runs[mid] >> 8) <= c) {
                a = uo_age_runs[mid] & 0xFF;
                lo = mid + 1;
            } else
                hi = mid - 1;
        }
    }
    info[0] = (uint8_t)(a >> 2);
    info[1] = (uint8_t)(a & 3);
    info[2] = info[3] = 0;
}

double uo_u_getNumericValue(UChar32 c)
{
    int lo = 0, hi = UO_NUM_RUNS - 1;

    if (!IN_RANGE(c) || !((uo_prop_of(c)->ea_nt_vo >> 3) & 3))
        return U_NO_NUMERIC_VALUE;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const struct uo_num *r = &uo_num_runs[mid];
        if (c < r->first)
            hi = mid - 1;
        else if (c >= r->first + r->count)
            lo = mid + 1;
        else
            return uo_num_values[r->value] + (double)(r->step * (c - r->first));
    }
    return U_NO_NUMERIC_VALUE;
}

int32_t uo_u_charDigitValue(UChar32 c)
{
    if (((uo_prop_of(c)->ea_nt_vo >> 3) & 3) != U_NT_DECIMAL)
        return -1;
    return (int32_t)uo_u_getNumericValue(c);
}

UBool uo_u_hasIDType(UChar32 c, UIdentifierType type)
{
    if ((unsigned)type > U_ID_TYPE_RECOMMENDED)
        return 0;
    return (uo_idtypes[moreOf(c)->idt] >> type) & 1;
}

int32_t uo_u_getIDTypes(UChar32 c, UIdentifierType *types, int32_t capacity, UErrorCode *err)
{
    unsigned bits;
    int32_t n = 0;
    int t;

    if (U_FAILURE(*err))
        return 0;
    if (capacity < 0 || (capacity > 0 && !types)) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    bits = uo_idtypes[moreOf(c)->idt];
    for (t = 0; t <= U_ID_TYPE_RECOMMENDED; t++)
        if ((bits >> t) & 1) {
            if (n < capacity)
                types[n] = (UIdentifierType)t;
            n++;
        }
    /* as ICU: a single type that never combines fits exactly; a list of
       those that combine is "overflow" unless there is room to spare */
    if (bits & (1u << U_ID_TYPE_NOT_XID | 1u << U_ID_TYPE_EXCLUSION | 1u << U_ID_TYPE_OBSOLETE |
                1u << U_ID_TYPE_TECHNICAL | 1u << U_ID_TYPE_UNCOMMON_USE | 1u << U_ID_TYPE_LIMITED_USE)) {
        if (n >= capacity)
            *err = U_BUFFER_OVERFLOW_ERROR;
    } else if (n > capacity)
        *err = U_BUFFER_OVERFLOW_ERROR;
    return n;
}

int32_t uo_uscript_getScriptExtensions(UChar32 c, UScriptCode *scripts, int32_t capacity, UErrorCode *err)
{
    const uint8_t *set;
    int32_t n, i;

    if (!err || U_FAILURE(*err))
        return 0;
    if (capacity < 0 || (capacity > 0 && !scripts)) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    set = IN_RANGE(c) ? scxOf(c) : 0;
    if (!set) {
        if (capacity == 0)
            *err = U_BUFFER_OVERFLOW_ERROR;
        else
            scripts[0] = (UScriptCode)uo_prop_of(c)->sc;
        return 1;
    }
    n = set[0];
    for (i = 0; i < n && i < capacity; i++)
        scripts[i] = (UScriptCode)set[i + 1];
    if (n > capacity)
        *err = U_BUFFER_OVERFLOW_ERROR;
    return n;
}

/* Every code point where one of our properties changes, once each or more
 * (ICU's *_addPropertyStarts: the sets of characterproperties.cpp are
 * built on them). The record trie is walked a block at a time, and a block
 * shared by many (the unassigned planes) costs one look. */
/* The starts of one trie's runs; a block shared by many (the unassigned
   planes) costs one look. */
#define WALK_TRIE(NAME, T1, T2, LEAF)                                                     \
    static void NAME(void (*add)(void *ctx, UChar32 c), void *ctx)                        \
    {                                                                                     \
        enum { N1 = 1 << T1##_B1, N2 = 1 << T1##_B2, SPAN = N1 * N2 };                    \
        int prev = -1, k, m, lastUniform = -2;                                            \
        unsigned lastMid = 0xFFFFFFFFu;                                                   \
        UChar32 c;                                                                        \
        for (c = 0; c < 0x110000; c += SPAN) {                                            \
            unsigned midOff = T2##_t1[c / SPAN];                                          \
            const uint16_t *mid = &T2##_t2[midOff];                                       \
            if (midOff != lastMid) {                                                      \
                lastMid = midOff;                                                         \
                lastUniform = LEAF[mid[0]];                                               \
                for (m = 0; m < N1 && lastUniform >= 0; m++)                              \
                    for (k = 0; k < N2; k++)                                              \
                        if (LEAF[mid[m] + k] != lastUniform) {                            \
                            lastUniform = -2;                                             \
                            break;                                                        \
                        }                                                                 \
            }                                                                             \
            if (lastUniform >= 0) {                                                       \
                if (lastUniform != prev) {                                                \
                    add(ctx, c);                                                          \
                    prev = lastUniform;                                                   \
                }                                                                         \
                continue;                                                                 \
            }                                                                             \
            for (m = 0; m < N1; m++)                                                      \
                for (k = 0; k < N2; k++) {                                                \
                    int v = LEAF[mid[m] + k];                                             \
                    if (v != prev) {                                                      \
                        add(ctx, c + m * N2 + k);                                         \
                        prev = v;                                                         \
                    }                                                                     \
                }                                                                         \
        }                                                                                 \
    }
WALK_TRIE(walkProps, UO_PROPS, uo_props, uo_props_leaf)
WALK_TRIE(walkBin, UO_BIN, uo_bin, uo_bin_leaf)
WALK_TRIE(walkMore, UO_MORE, uo_more, uo_more_leaf)

/* Every code point where one of our properties changes, once each or more
 * (ICU's *_addPropertyStarts: the sets of characterproperties.cpp are
 * built on them). */
void uo_prop_starts(void (*add)(void *ctx, UChar32 c), void *ctx)
{
    int i, k;

    walkProps(add, ctx);
    walkBin(add, ctx);
    walkMore(add, ctx);
    for (i = 0; i < UO_AGE_RUNS; i++)
        add(ctx, (UChar32)(uo_age_runs[i] >> 8));
    for (i = 0; i < UO_NUM_RUNS; i++) {
        const struct uo_num *r = &uo_num_runs[i];
        for (k = 0; k <= (r->step ? r->count : 0); k++)
            add(ctx, r->first + k);
        add(ctx, r->first + r->count);
    }
    for (i = 0; i < UO_FCD_COUNT; i++) {
        add(ctx, (UChar32)(uo_fcd_lead[i] >> 8));
        add(ctx, (UChar32)(uo_fcd_lead[i] >> 8) + 1);
    }
    for (i = 0; i < UO_BLOCK_COUNT; i++) {
        add(ctx, uo_blocks[i].first);
        add(ctx, uo_blocks[i].last + 1);
    }
    for (i = 0; i < UO_SCX_COUNT; i++) {
        add(ctx, uo_scx[i].first);
        add(ctx, uo_scx[i].last + 1);
    }
    for (i = 0; i < UO_BIDI_STARTS; i++)
        add(ctx, uo_bidi_starts[i]);
    for (i = 0; i < UO_HARD_STARTS; i++)
        add(ctx, uo_hard_starts[i]);
}

/* uchar.cpp's tests, from the general category and a few code points */
#define TAB 9
#define IS_CONTROL_SPACE(c) ((c) <= 0x9F && (((c) >= TAB && (c) <= 0xD) || ((c) >= 0x1C && (c) <= 0x1F) || (c) == 0x85))
#define IS_ASCII_CONTROL_SPACE(c) ((c) <= 0x1F && (c) >= TAB && ((c) <= 0xD || (c) >= 0x1C))

UBool uo_u_islower(UChar32 c) { return gcOf(c) == U_LOWERCASE_LETTER; }
UBool uo_u_isupper(UChar32 c) { return gcOf(c) == U_UPPERCASE_LETTER; }
UBool uo_u_istitle(UChar32 c) { return gcOf(c) == U_TITLECASE_LETTER; }
UBool uo_u_isdefined(UChar32 c) { return gcOf(c) != U_UNASSIGNED; }

UBool uo_u_isxdigit(UChar32 c)
{
    if ((c <= 0x66 && c >= 0x41 && (c <= 0x46 || c >= 0x61)) ||
        (c >= 0xFF21 && c <= 0xFF46 && (c <= 0xFF26 || c >= 0xFF41)))
        return 1;
    return gcOf(c) == U_DECIMAL_DIGIT_NUMBER;
}

UBool uo_u_isbase(UChar32 c)
{
    return (MASK(gcOf(c)) & (GC_L | U_GC_N_MASK | U_GC_MC_MASK | U_GC_ME_MASK)) != 0;
}

UBool uo_u_iscntrl(UChar32 c)
{
    return (MASK(gcOf(c)) & (U_GC_CC_MASK | U_GC_CF_MASK | U_GC_ZL_MASK | U_GC_ZP_MASK)) != 0;
}

UBool uo_u_isISOControl(UChar32 c)
{
    return (uint32_t)c <= 0x9F && (c <= 0x1F || c >= 0x7F);
}

UBool uo_u_isspace(UChar32 c)
{
    return (MASK(gcOf(c)) & GC_Z) != 0 || IS_CONTROL_SPACE(c);
}

UBool uo_u_isJavaSpaceChar(UChar32 c)
{
    return (MASK(gcOf(c)) & GC_Z) != 0;
}

UBool uo_u_isWhitespace(UChar32 c)
{
    return ((MASK(gcOf(c)) & GC_Z) != 0 && c != 0xA0 && c != 0x2007 && c != 0x202F) ||
           IS_ASCII_CONTROL_SPACE(c);
}

UBool uo_u_isblank(UChar32 c)
{
    if ((uint32_t)c <= 0x9F)
        return c == 9 || c == 0x20;
    return gcOf(c) == U_SPACE_SEPARATOR;
}

UBool uo_u_isgraph(UChar32 c)
{
    return (MASK(gcOf(c)) & (U_GC_CC_MASK | U_GC_CF_MASK | U_GC_CS_MASK | U_GC_CN_MASK | GC_Z)) == 0;
}

UBool uo_u_isIDIgnorable(UChar32 c)
{
    if (c <= 0x9F)
        return uo_u_isISOControl(c) && !IS_ASCII_CONTROL_SPACE(c);
    return gcOf(c) == U_FORMAT_CHAR;
}

UBool uo_u_isJavaIDStart(UChar32 c)
{
    return (MASK(gcOf(c)) & (GC_L | U_GC_SC_MASK | U_GC_PC_MASK)) != 0;
}

UBool uo_u_isJavaIDPart(UChar32 c)
{
    return (MASK(gcOf(c)) & (U_GC_ND_MASK | U_GC_NL_MASK | GC_L | U_GC_SC_MASK | U_GC_PC_MASK |
                             U_GC_MC_MASK | U_GC_MN_MASK)) != 0 || uo_u_isIDIgnorable(c);
}

int32_t uo_u_digit(UChar32 ch, int8_t radix)
{
    int8_t value;

    if ((uint8_t)(radix - 2) <= (36 - 2)) {
        value = (int8_t)uo_u_charDigitValue(ch);
        if (value < 0) {
            if (ch >= 0x61 && ch <= 0x7A)
                value = (int8_t)(ch - 0x57);
            else if (ch >= 0x41 && ch <= 0x5A)
                value = (int8_t)(ch - 0x37);
            else if (ch >= 0xFF41 && ch <= 0xFF5A)
                value = (int8_t)(ch - 0xFF37);
            else if (ch >= 0xFF21 && ch <= 0xFF3A)
                value = (int8_t)(ch - 0xFF17);
        }
    } else
        value = -1;
    return (int8_t)(value < radix ? value : -1);
}

UChar32 uo_u_forDigit(int32_t digit, int8_t radix)
{
    if ((uint8_t)(radix - 2) > (36 - 2) || (uint32_t)digit >= (uint32_t)radix)
        return 0;
    return digit < 10 ? (UChar32)(0x30 + digit) : (UChar32)(0x61 - 10 + digit);
}

void uo_u_getUnicodeVersion(UVersionInfo info)
{
    if (info) {
        memset(info, 0, U_MAX_VERSION_LENGTH);
        info[0] = UO_UNICODE_MAJOR;
        info[1] = UO_UNICODE_MINOR;
        info[2] = UO_UNICODE_MICRO;
    }
}

struct enumTypes {
    UCharEnumTypeRange *fn;
    const void *ctx;
    UChar32 start;
    int type, stop;
};

static void enumTypeStart(void *p, UChar32 c)
{
    struct enumTypes *e = (struct enumTypes *)p;
    int t = (int)gcOf(c);

    if (e->stop || t == e->type)
        return;
    if (e->type >= 0 && !e->fn(e->ctx, e->start, c, (UCharCategory)e->type))
        e->stop = 1;
    e->start = c;
    e->type = t;
}

void uo_u_enumCharTypes(UCharEnumTypeRange *enumRange, const void *context)
{
    struct enumTypes e;
    UChar32 c;
    int prev = -1;

    if (!enumRange)
        return;
    e.fn = enumRange;
    e.ctx = context;
    e.start = 0;
    e.type = -1;
    e.stop = 0;
    /* the starts of the record trie's runs, in order: only those can change
       the general category */
    for (c = 0; c < 0x110000 && !e.stop; c++) {
        int v = UO_PROPS(c);
        if (v != prev) {
            enumTypeStart(&e, c);
            prev = v;
        }
    }
    if (!e.stop && e.type >= 0)
        enumRange(context, e.start, 0x110000, (UCharCategory)e.type);
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
