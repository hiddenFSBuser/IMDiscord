#pragma once

void ui_init();
void ui_frame();
void ui_shutdown();

// True when something on screen moves by itself - a call, a picture arriving,
// the store changing - and the next frame cannot wait for input.
bool ui_wants_redraw();
