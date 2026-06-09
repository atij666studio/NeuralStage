// Non-macOS implementation of ns::isSystemModalActive.
// macOS uses SystemModal.mm (AppKit). This file is compiled on Windows/Linux
// where there is no equivalent system-modal concept the host needs to wait for.

#include "SystemModal.h"

#if ! defined (__APPLE__)
namespace ns
{
    bool isSystemModalActive() { return false; }
}
#endif
