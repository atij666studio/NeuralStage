#include "AppState.h"

AppState::AppState()
{
    state.setProperty ("presetName", "Default", nullptr);
    state.setProperty ("activeScene", 0, nullptr);
}
