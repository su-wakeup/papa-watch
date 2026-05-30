// face_manager — hosts an LVGL tileview that lets the user swipe horizontally
// between watch faces. Each face is mounted on its own tile.
//
// At v0.6.0 it carries:
//   tile 0 — face_lcd (Casio-style backlit LCD)
//   tile 1 — face_mech (mechanical analog, LILYGO assets) — placeholder for now
//   tile 2 — reserved
//
// The active tile is persisted in NVS so the watch wakes up showing the same
// face it was last viewed on.

#pragma once

namespace face_manager {

void create();
void destroy();         // teardown for app exit — clears screen + nulls children
void update();          // forwards refresh to the active face
int  currentIndex();

}  // namespace face_manager
