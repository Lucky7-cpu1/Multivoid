// ui/fear_pane.h -- the F1 > Fear director pane.
//
// The host's controls for coop/fear: switch it on or off, how hard it pushes, whether it holds while
// someone sleeps and whether it may start the deadly invasions; the AI agent's switch, key, model and
// email language; the live tension meter with the reason it is holding, the agent's last decision and
// the recent scares; and buttons to fire one catalog scare now, which is how two players check that a
// scare reaches both of them. A client sees one line saying the host runs the director. Settings are
// written through coop::config::SetValue (local rows; the director reads them on the host).

#pragma once

namespace ui::fear_pane {

// Draw the pane's content (dev_menu's content child). Render thread.
void Render();

}  // namespace ui::fear_pane
