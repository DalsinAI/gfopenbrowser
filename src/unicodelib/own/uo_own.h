/* unicode.library, our own code: the calls own.txt names.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Each is ICU's C call of the same name without "uo_", with the same
 * arguments and result; gen_calls.py points the library's table at it.
 */
#ifndef UO_OWN_H
#define UO_OWN_H

#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utext.h>
#include <unicode/utypes.h>
#include <unicode/uversion.h>

struct uo_prop;
const struct uo_prop *uo_prop_of(UChar32 c);
int uo_has_binary(UChar32 c, int bit);

/* uo_props.c */
int8_t uo_u_charType(UChar32 c);
UCharDirection uo_u_charDirection(UChar32 c);
uint8_t uo_u_getCombiningClass(UChar32 c);
UChar32 uo_u_charMirror(UChar32 c);
UBool uo_u_isalpha(UChar32 c);
UBool uo_u_isalnum(UChar32 c);
UBool uo_u_isdigit(UChar32 c);
UBool uo_u_ispunct(UChar32 c);
UBool uo_u_isprint(UChar32 c);
UBool uo_u_isUWhiteSpace(UChar32 c);
UBool uo_u_hasBinaryProperty(UChar32 c, UProperty which);
int32_t uo_u_getIntPropertyValue(UChar32 c, UProperty which);
int32_t uo_u_getPropertyValueEnum(UProperty property, const char *alias);
UBlockCode uo_ublock_getCode(UChar32 c);
UScriptCode uo_uscript_getScript(UChar32 c, UErrorCode *err);
UBool uo_uscript_hasScript(UChar32 c, UScriptCode sc);
const char *uo_uscript_getShortName(UScriptCode sc);
UBool uo_uscript_isRightToLeft(UScriptCode sc);
void uo_u_getVersion(UVersionInfo info);
const char *uo_u_errorName(UErrorCode code);
int32_t uo_u_countChar32(const UChar *s, int32_t length);

/* the rest of ICU's uchar.cpp and uprops.cpp (uo_icu.c answers ICU's own
   code with them under ICU's names) */
int32_t uo_u_getIntPropertyMaxValue(UProperty which);
void uo_u_charAge(UChar32 c, UVersionInfo info);
double uo_u_getNumericValue(UChar32 c);
int32_t uo_u_charDigitValue(UChar32 c);
UBool uo_u_hasIDType(UChar32 c, UIdentifierType type);
int32_t uo_u_getIDTypes(UChar32 c, UIdentifierType *types, int32_t capacity, UErrorCode *err);
int32_t uo_uscript_getScriptExtensions(UChar32 c, UScriptCode *scripts, int32_t capacity, UErrorCode *err);
void uo_prop_starts(void (*add)(void *ctx, UChar32 c), void *ctx);
UBool uo_u_islower(UChar32 c);
UBool uo_u_isupper(UChar32 c);
UBool uo_u_istitle(UChar32 c);
UBool uo_u_isdefined(UChar32 c);
UBool uo_u_isxdigit(UChar32 c);
UBool uo_u_isbase(UChar32 c);
UBool uo_u_iscntrl(UChar32 c);
UBool uo_u_isISOControl(UChar32 c);
UBool uo_u_isspace(UChar32 c);
UBool uo_u_isJavaSpaceChar(UChar32 c);
UBool uo_u_isWhitespace(UChar32 c);
UBool uo_u_isblank(UChar32 c);
UBool uo_u_isgraph(UChar32 c);
UBool uo_u_isIDIgnorable(UChar32 c);
UBool uo_u_isJavaIDStart(UChar32 c);
UBool uo_u_isJavaIDPart(UChar32 c);
int32_t uo_u_digit(UChar32 ch, int8_t radix);
UChar32 uo_u_forDigit(int32_t digit, int8_t radix);
void uo_u_getUnicodeVersion(UVersionInfo info);
void uo_u_enumCharTypes(UCharEnumTypeRange *enumRange, const void *context);

/* uo_case.c */
UChar32 uo_u_tolower(UChar32 c);
UChar32 uo_u_toupper(UChar32 c);
UChar32 uo_u_totitle(UChar32 c);
UChar32 uo_u_foldCase(UChar32 c, uint32_t options);
int32_t uo_u_strToLower(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                        const char *locale, UErrorCode *err);
int32_t uo_u_strToUpper(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                        const char *locale, UErrorCode *err);
int32_t uo_u_strToTitle(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                        UBreakIterator *titleIter, const char *locale, UErrorCode *err);
int32_t uo_u_strFoldCase(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                         uint32_t options, UErrorCode *err);

/* uo_brk.c */
UBreakIterator *uo_ubrk_open(UBreakIteratorType type, const char *locale, const UChar *text,
                             int32_t textLength, UErrorCode *status);
UBreakIterator *uo_ubrk_clone(const UBreakIterator *bi, UErrorCode *status);
void uo_ubrk_close(UBreakIterator *bi);
void uo_ubrk_setText(UBreakIterator *bi, const UChar *text, int32_t textLength, UErrorCode *status);
void uo_ubrk_setUText(UBreakIterator *bi, UText *text, UErrorCode *status);
int32_t uo_ubrk_current(const UBreakIterator *bi);
int32_t uo_ubrk_first(UBreakIterator *bi);
int32_t uo_ubrk_last(UBreakIterator *bi);
int32_t uo_ubrk_next(UBreakIterator *bi);
int32_t uo_ubrk_previous(UBreakIterator *bi);
int32_t uo_ubrk_following(UBreakIterator *bi, int32_t offset);
int32_t uo_ubrk_preceding(UBreakIterator *bi, int32_t offset);
UBool uo_ubrk_isBoundary(UBreakIterator *bi, int32_t offset);
int32_t uo_ubrk_getRuleStatus(UBreakIterator *bi);

/* uo_utext.c */
UText *uo_utext_setup(UText *ut, int32_t extraSpace, UErrorCode *status);
UText *uo_utext_close(UText *ut);

/* ICU's bidi properties (ubidi_props.h) */
UJoiningType uo_ubidi_getJoiningType(UChar32 c);
UJoiningGroup uo_ubidi_getJoiningGroup(UChar32 c);
UBidiPairedBracketType uo_ubidi_getPairedBracketType(UChar32 c);
UChar32 uo_u_getBidiPairedBracket(UChar32 c);
int32_t uo_ubidi_getMaxValue(UProperty which);

#endif
