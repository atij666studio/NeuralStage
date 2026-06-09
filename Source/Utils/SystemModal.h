#pragma once

namespace ns
{
    /** Returns true if any system-level modal dialog (e.g. Gatekeeper /
     *  "Move to Bin" / NSAlert) is currently presented by the running app.
     *  Implemented via AppKit on macOS; returns false on other platforms.
     */
    bool isSystemModalActive();
}
