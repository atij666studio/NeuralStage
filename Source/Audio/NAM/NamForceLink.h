#pragma once

/// Ensure the NAM "SlimmableContainer" architecture parser is registered in
/// ConfigParserRegistry before any get_dsp() call.
///
/// Call this (or reference it) from a non-optimizable context — the file-scope
/// NamRegistrationGuard struct in NAMProcessor.cpp does this automatically.
/// Defined in NamForceLink.cpp.
void ns_ensureNamContainerRegistered() noexcept;
