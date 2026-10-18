// ---------------------------------------------------------------------------
// Il registro dei bersagli, visto da fuori: una domanda sola.
// ---------------------------------------------------------------------------
#pragma once

#include "app_state.h"

namespace Bolt
{
// Il bersaglio di tipo |kind| sotto il punto, o niente. Si legge **al contrario**:
// l'ultimo registrato e' quello disegnato sopra, ed e' quello che prende il clic.
const ClickTarget* HitTest(const AppState& app, float x, float y, ClickTarget::Kind kind);
} // namespace Bolt
