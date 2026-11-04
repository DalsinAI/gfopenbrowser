/* unicode.library, our own code: ICU's internal calls, answered by ours.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The ICU code still in the library (its bidi algorithm, IDNA, property
 * sets, ...) calls ICU's internal functions by their ICU 78 names. Each
 * group here gives every function of one ICU object file under that name,
 * so the linker never takes ICU's file (and its tables) from libicuuc.a:
 * one missing name and ICU's file comes back, and the link stops on the
 * names both define.
 *
 * Only in the library: the host checks (tests/) link ICU itself and
 * compare it with the uo_ calls these forward to.
 */
#include <unicode/uchar.h>
#include <unicode/uset.h>
#include <unicode/utypes.h>

#include "uo_data.h"
#include "uo_own.h"

/* ICU's USetAdder (uset_imp.h), as ICU 78 lays it out */
typedef void USetAdd(USet *set, UChar32 c);
typedef void USetAddRange(USet *set, UChar32 start, UChar32 end);
typedef void USetAddString(USet *set, const UChar *str, int32_t length);
typedef void USetRemove(USet *set, UChar32 c);
typedef void USetRemoveRange(USet *set, UChar32 start, UChar32 end);
typedef struct USetAdder {
    USet *set;
    USetAdd *add;
    USetAddRange *addRange;
    USetAddString *addString;
    USetRemove *remove;
    USetRemoveRange *removeRange;
} USetAdder;

/* ---- ubidi_props.o --------------------------------------------------- */

void ubidi_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode);
int32_t ubidi_getMaxValue(UProperty which);
UCharDirection ubidi_getClass(UChar32 c);
UBool ubidi_isMirrored(UChar32 c);
UChar32 ubidi_getMirror(UChar32 c);
UBool ubidi_isBidiControl(UChar32 c);
UBool ubidi_isJoinControl(UChar32 c);
UJoiningType ubidi_getJoiningType(UChar32 c);
UJoiningGroup ubidi_getJoiningGroup(UChar32 c);
UBidiPairedBracketType ubidi_getPairedBracketType(UChar32 c);
UChar32 ubidi_getPairedBracket(UChar32 c);

void ubidi_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode)
{
    int i;
    if (U_FAILURE(*pErrorCode))
        return;
    for (i = 0; i < UO_BIDI_STARTS; i++)
        sa->add(sa->set, uo_bidi_starts[i]);
}

int32_t ubidi_getMaxValue(UProperty which)
{
    return uo_ubidi_getMaxValue(which);
}

UCharDirection ubidi_getClass(UChar32 c)
{
    return uo_u_charDirection(c);
}

UBool ubidi_isMirrored(UChar32 c)
{
    return uo_has_binary(c, uo_bin_bit[UCHAR_BIDI_MIRRORED]);
}

UChar32 ubidi_getMirror(UChar32 c)
{
    return uo_u_charMirror(c);
}

UBool ubidi_isBidiControl(UChar32 c)
{
    return uo_has_binary(c, uo_bin_bit[UCHAR_BIDI_CONTROL]);
}

UBool ubidi_isJoinControl(UChar32 c)
{
    return uo_has_binary(c, uo_bin_bit[UCHAR_JOIN_CONTROL]);
}

UJoiningType ubidi_getJoiningType(UChar32 c)
{
    return uo_ubidi_getJoiningType(c);
}

UJoiningGroup ubidi_getJoiningGroup(UChar32 c)
{
    return uo_ubidi_getJoiningGroup(c);
}

UBidiPairedBracketType ubidi_getPairedBracketType(UChar32 c)
{
    return uo_ubidi_getPairedBracketType(c);
}

UChar32 ubidi_getPairedBracket(UChar32 c)
{
    return uo_u_getBidiPairedBracket(c);
}

/* the public ones ubidi_props.o defines too */
UCharDirection u_charDirection(UChar32 c)
{
    return uo_u_charDirection(c);
}

UBool u_isMirrored(UChar32 c)
{
    return uo_has_binary(c, uo_bin_bit[UCHAR_BIDI_MIRRORED]);
}

UChar32 u_charMirror(UChar32 c)
{
    return uo_u_charMirror(c);
}

UChar32 u_getBidiPairedBracket(UChar32 c)
{
    return uo_u_getBidiPairedBracket(c);
}

/* ---- uchar.o and uprops.o -------------------------------------------- */
/* Every name of the two that the ICU code still in the library calls.
 * Not here, as nothing calls them: u_getMainProperties,
 * u_getUnicodeProperties and uprv_getMaxValues (uchar.o's data words, only
 * uprops.o read them), u_stringHasBinaryProperty and u_getFC_NFKC_Closure. */

/* ICU's UPropertySource (uprops.h), as ICU 78 numbers it */
enum {
    SRC_NONE, SRC_CHAR, SRC_PROPSVEC, SRC_NAMES, SRC_CASE, SRC_BIDI, SRC_CHAR_AND_PROPSVEC,
    SRC_CASE_AND_NORM, SRC_NFC, SRC_NFKC, SRC_NFKC_CF, SRC_NFC_CANON_ITER, SRC_INPC, SRC_INSC,
    SRC_VO, SRC_EMOJI, SRC_IDSU, SRC_ID_COMPAT_MATH, SRC_BLOCK, SRC_MCM, SRC_COUNT
};
#define UNICODE_1_NAME 0x400B          /* UCHAR_UNICODE_1_NAME, deprecated */

int8_t u_charType(UChar32 c) { return uo_u_charType(c); }
UBool u_islower(UChar32 c) { return uo_u_islower(c); }
UBool u_isupper(UChar32 c) { return uo_u_isupper(c); }
UBool u_istitle(UChar32 c) { return uo_u_istitle(c); }
UBool u_isdigit(UChar32 c) { return uo_u_isdigit(c); }
UBool u_isxdigit(UChar32 c) { return uo_u_isxdigit(c); }
UBool u_isalpha(UChar32 c) { return uo_u_isalpha(c); }
UBool u_isUAlphabetic(UChar32 c) { return uo_has_binary(c, uo_bin_bit[UCHAR_ALPHABETIC]); }
UBool u_isalnum(UChar32 c) { return uo_u_isalnum(c); }
UBool u_isdefined(UChar32 c) { return uo_u_isdefined(c); }
UBool u_isbase(UChar32 c) { return uo_u_isbase(c); }
UBool u_iscntrl(UChar32 c) { return uo_u_iscntrl(c); }
UBool u_isISOControl(UChar32 c) { return uo_u_isISOControl(c); }
UBool u_isspace(UChar32 c) { return uo_u_isspace(c); }
UBool u_isJavaSpaceChar(UChar32 c) { return uo_u_isJavaSpaceChar(c); }
UBool u_isWhitespace(UChar32 c) { return uo_u_isWhitespace(c); }
UBool u_isblank(UChar32 c) { return uo_u_isblank(c); }
UBool u_isUWhiteSpace(UChar32 c) { return uo_u_isUWhiteSpace(c); }
UBool u_isprint(UChar32 c) { return uo_u_isprint(c); }
UBool u_isgraph(UChar32 c) { return uo_u_isgraph(c); }
UBool u_ispunct(UChar32 c) { return uo_u_ispunct(c); }
UBool u_isIDIgnorable(UChar32 c) { return uo_u_isIDIgnorable(c); }
UBool u_isJavaIDStart(UChar32 c) { return uo_u_isJavaIDStart(c); }
UBool u_isJavaIDPart(UChar32 c) { return uo_u_isJavaIDPart(c); }
int32_t u_charDigitValue(UChar32 c) { return uo_u_charDigitValue(c); }
double u_getNumericValue(UChar32 c) { return uo_u_getNumericValue(c); }
int32_t u_digit(UChar32 ch, int8_t radix) { return uo_u_digit(ch, radix); }
UChar32 u_forDigit(int32_t digit, int8_t radix) { return uo_u_forDigit(digit, radix); }
void u_getUnicodeVersion(UVersionInfo info) { uo_u_getUnicodeVersion(info); }
void u_charAge(UChar32 c, UVersionInfo info) { uo_u_charAge(c, info); }
void u_enumCharTypes(UCharEnumTypeRange *enumRange, const void *context) { uo_u_enumCharTypes(enumRange, context); }
UBlockCode ublock_getCode(UChar32 c) { return uo_ublock_getCode(c); }
UScriptCode uscript_getScript(UChar32 c, UErrorCode *err) { return uo_uscript_getScript(c, err); }
UBool uscript_hasScript(UChar32 c, UScriptCode sc) { return uo_uscript_hasScript(c, sc); }

int32_t uscript_getScriptExtensions(UChar32 c, UScriptCode *scripts, int32_t capacity, UErrorCode *err)
{
    return uo_uscript_getScriptExtensions(c, scripts, capacity, err);
}

/* uchar.cpp's internal ones (uprops.h) */
UBool u_isalnumPOSIX(UChar32 c);
UBool u_isgraphPOSIX(UChar32 c);
UBool u_isprintPOSIX(UChar32 c);
void uchar_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode);
void upropsvec_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode);
void uprv_addScriptExtensionsCodePoints(const USetAdder *sa, UErrorCode *pErrorCode);
int32_t uprops_getSource(UProperty which);
void uprops_addPropertyStarts(int32_t src, const USetAdder *sa, UErrorCode *pErrorCode);

UBool u_isalnumPOSIX(UChar32 c) { return uo_u_hasBinaryProperty(c, UCHAR_POSIX_ALNUM); }
UBool u_isgraphPOSIX(UChar32 c) { return uo_u_hasBinaryProperty(c, UCHAR_POSIX_GRAPH); }
UBool u_isprintPOSIX(UChar32 c) { return uo_u_hasBinaryProperty(c, UCHAR_POSIX_PRINT); }

static void addStart(void *ctx, UChar32 c)
{
    const USetAdder *sa = (const USetAdder *)ctx;
    if (c <= 0x10FFFF)
        sa->add(sa->set, c);
}

/* One set of starts serves every source of ours: it has the start of every
 * run of every property we answer, which is all a source's set needs. */
static void allStarts(const USetAdder *sa, UErrorCode *pErrorCode)
{
    if (U_FAILURE(*pErrorCode))
        return;
    uo_prop_starts(addStart, (void *)sa);
}

void uchar_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode) { allStarts(sa, pErrorCode); }
void upropsvec_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode) { allStarts(sa, pErrorCode); }
void uprops_addPropertyStarts(int32_t src, const USetAdder *sa, UErrorCode *pErrorCode)
{
    (void)src;
    allStarts(sa, pErrorCode);
}

/* ICU declares it extern "C" with UErrorCode &, a pointer underneath */
void ublock_addPropertyStarts(const USetAdder *sa, UErrorCode *errorCode);
void ublock_addPropertyStarts(const USetAdder *sa, UErrorCode *errorCode)
{
    int i;
    (void)errorCode;
    for (i = 0; i < UO_BLOCK_COUNT; i++) {
        sa->add(sa->set, uo_blocks[i].first);
        sa->add(sa->set, uo_blocks[i].last + 1);
    }
}

void uprv_addScriptExtensionsCodePoints(const USetAdder *sa, UErrorCode *pErrorCode)
{
    int i;
    if (U_FAILURE(*pErrorCode))
        return;
    for (i = 0; i < UO_SCX_COUNT; i++)
        sa->addRange(sa->set, uo_scx[i].first, uo_scx[i].last);
}

/* where characterproperties.cpp takes the starts of a property's runs from:
   ours (SRC_CHAR) for everything we answer; ICU's own for the strings of
   case mappings and names, and for the emoji sets of strings */
int32_t uprops_getSource(UProperty which)
{
    if (which < UCHAR_BINARY_START)
        return SRC_NONE;
    if (which < UCHAR_BINARY_LIMIT)
        return which >= UCHAR_BASIC_EMOJI && which <= UCHAR_RGI_EMOJI ? SRC_EMOJI : SRC_CHAR;
    if (which < UCHAR_INT_START)
        return SRC_NONE;
    if (which < UCHAR_INT_LIMIT)
        return SRC_CHAR;
    switch ((int)which) {
    case UCHAR_GENERAL_CATEGORY_MASK:
    case UCHAR_NUMERIC_VALUE:
    case UCHAR_AGE:
    case UCHAR_SCRIPT_EXTENSIONS:
    case UCHAR_IDENTIFIER_TYPE:
        return SRC_CHAR;
    case UCHAR_BIDI_MIRRORING_GLYPH:
    case UCHAR_BIDI_PAIRED_BRACKET:
        return SRC_BIDI;
    case UCHAR_CASE_FOLDING:
    case UCHAR_LOWERCASE_MAPPING:
    case UCHAR_SIMPLE_CASE_FOLDING:
    case UCHAR_SIMPLE_LOWERCASE_MAPPING:
    case UCHAR_SIMPLE_TITLECASE_MAPPING:
    case UCHAR_SIMPLE_UPPERCASE_MAPPING:
    case UCHAR_TITLECASE_MAPPING:
    case UCHAR_UPPERCASE_MAPPING:
        return SRC_CASE;
    case UCHAR_ISO_COMMENT:
    case UCHAR_NAME:
    case UNICODE_1_NAME:
        return SRC_NAMES;
    default:
        return SRC_NONE;
    }
}

/* uprops.cpp's public ones */
UBool u_hasBinaryProperty(UChar32 c, UProperty which) { return uo_u_hasBinaryProperty(c, which); }
int32_t u_getIntPropertyValue(UChar32 c, UProperty which) { return uo_u_getIntPropertyValue(c, which); }
int32_t u_getIntPropertyMinValue(UProperty which) { (void)which; return 0; }
int32_t u_getIntPropertyMaxValue(UProperty which) { return uo_u_getIntPropertyMaxValue(which); }
UBool u_isIDStart(UChar32 c) { return uo_has_binary(c, uo_bin_bit[UCHAR_ID_START]); }
UBool u_isIDPart(UChar32 c) { return uo_has_binary(c, uo_bin_bit[UCHAR_ID_CONTINUE]); }
bool u_hasIDType(UChar32 c, UIdentifierType type) { return uo_u_hasIDType(c, type); }

int32_t u_getIDTypes(UChar32 c, UIdentifierType *types, int32_t capacity, UErrorCode *err)
{
    return uo_u_getIDTypes(c, types, capacity, err);
}
