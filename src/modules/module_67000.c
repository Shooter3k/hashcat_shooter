/**
 * Mode 67000 compatibility module, forward-ported from hashcat-beta.
 *
 * Upstream renumbered this yescrypt implementation to mode 36100. Keeping a
 * shared implementation lets old 67000 command lines continue to work while
 * retaining the current module API and the newer performance fixes.
 */

// Keep the legacy command-line mode number while selecting the maintained
// mode-36100 kernel. The upstream yescrypt temporary-buffer layout changed in
// 9c735bade, so pairing the new shared module with the old m67000 kernel would
// corrupt the device/host buffer contract.

#define YESCRYPT_KERN_TYPE 36100

#include "module_36100.c"
