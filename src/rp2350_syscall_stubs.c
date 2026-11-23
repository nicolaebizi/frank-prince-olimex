/*
 * Minimal newlib syscall stubs missing from the pico-sdk newlib interface.
 *
 * The pico-sdk provides __weak _exit/_sbrk/_gettimeofday/_times/_getpid/
 * _kill/_read/_write/_open/_close/_lseek/_fstat/_isatty
 * (pico-sdk/src/rp2_common/pico_clib_interface/newlib_interface.c), but the
 * full-game link pulls in SDLPoP's options.c, which calls stat()/unlink().
 * newlib's reentrant wrappers (_stat_r in libc_a-statr.o, _unlink_r in
 * libc_a-unlinkr.o) reference the non-reentrant syscalls _stat/_unlink,
 * which are NOT provided by the SDK -> undefined references at link time.
 *
 * Provide them as __weak stubs (same convention as the SDK), failing with
 * ENOENT: on this bare-metal target there is no POSIX filesystem backing
 * these calls (the game uses FatFS), and the only SDLPoP call sites are the
 * optional mod-folder checks, which degrade to "not found" gracefully.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <sys/stat.h>

#include "pico.h"

__weak int _stat(const char *__restrict path, struct stat *__restrict st) {
    (void)path;
    (void)st;
    errno = ENOENT;
    return -1;
}

__weak int _unlink(const char *path) {
    (void)path;
    errno = ENOENT;
    return -1;
}
