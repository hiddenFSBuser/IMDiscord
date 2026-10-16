#pragma once
#include "ui_state.h"

// Chat wallpaper: a picture behind the whole client, with dimming and blur.
//
// The cropped result lives in %LOCALAPPDATA%\IMDiscord\wallpaper.png; the
// settings only carry on/off, dim 0..100 and blur 0..20.
namespace wallpaper
{
    bool enabled();     // the setting is on (even if the texture is missing)
    bool active();      // on and a texture is ready to draw
    void draw_under(ImVec2 origin, ImVec2 size);   // inside a window, after Begin
    void draw_background(ImVec2 origin, ImVec2 size);   // on the background layer
    ImU32 panel(ImU32 base);   // background fill with alpha while active
    ImVec4 tint(ImVec4 v);     // any style color with alpha while active
    int dim();                 // 0..100
    void set_dim(int v);
    int blur();                // 0..20
    void set_blur(int v);
    void pick_file();          // file dialog -> aspect crop -> save
    void crop_current();       // re-crop the saved file
    void remove();             // off + forget the file
    void reload();             // (re)read the file, blur, upload
}

void ui_view_wallpaper_crop();
void ui_wallpaper_settings();
