/* GFOpenBrowser (DalsinAI/openamigabrowser). Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE. */
// libstdc++'s std::filesystem::space() calls statvfs(), which the amigaos
// <sys/statvfs.h> declares without extern "C" and libnix does not provide.
// Report it as unsupported; space() then returns an error_code.
#include <errno.h>
#include <sys/statvfs.h>

int statvfs(const char*, struct statvfs*)
{
    errno = ENOSYS;
    return -1;
}
