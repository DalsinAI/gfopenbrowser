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
