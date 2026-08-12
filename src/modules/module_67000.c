/**
 * Mode 67000 compatibility module, forward-ported from hashcat-beta.
 *
 * Upstream renumbered this yescrypt implementation to mode 36100. Keeping a
 * shared implementation lets old 67000 command lines continue to work while
 * retaining the current module API and the newer performance fixes.
 */

#define YESCRYPT_KERN_TYPE 67000

#include "module_36100.c"
