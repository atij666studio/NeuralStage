#include "SystemModal.h"

#if JUCE_MAC
 #import <AppKit/AppKit.h>
#endif

namespace ns
{
    bool isSystemModalActive()
    {
       #if JUCE_MAC
        @autoreleasepool
        {
            if (NSApp == nil) return false;
            if ([NSApp modalWindow] != nil) return true;
            // Walk windows looking for a sheet or modal-style alert.
            for (NSWindow* w in [NSApp windows])
            {
                if (w == nil) continue;
                if ([w isVisible] && ([w isSheet] || [w level] >= NSModalPanelWindowLevel))
                {
                    // Skip our own document windows (which sit at NormalWindowLevel).
                    if ([w level] > NSNormalWindowLevel)
                        return true;
                }
            }
            return false;
        }
       #else
        return false;
       #endif
    }
}
