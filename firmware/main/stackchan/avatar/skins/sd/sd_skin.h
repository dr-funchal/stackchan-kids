/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../../avatar/avatar.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <string>
#include <vector>

namespace stackchan::avatar {

/**
 * @brief Face loaded from the microSD card: skins/<name>/fundo.png (320x240 background) plus optional
 * skins/<name>/skin.json {"olhos":"#RRGGBB","palpebra":"#RRGGBB","boca":"#RRGGBB","presas":bool}.
 * Eyes are drawn at (90,104)/(230,104) and the mouth at (160,154), like the panda faces
 */
class SdSkinAvatar : public Avatar {
public:
    // Read the skin files; call before init(). Returns false if the skin is missing or broken
    bool load(const std::string& dir);
    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);
    uitk::lvgl_cpp::Container* getPanel() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _background;
    lv_color_t _eye_color    = lv_color_white();
    lv_color_t _eyelid_color = lv_color_black();
    lv_color_t _mouth_color  = lv_color_black();
    bool _fangs              = false;
};

}  // namespace stackchan::avatar

namespace sd_skin {
// Folder of the skin chosen in skins/ativa.txt, or "" for the built-in face
std::string activeSkinDir();
std::vector<std::string> list();
// "padrao" (or empty) goes back to the built-in face. Takes effect after a restart
bool setActive(const std::string& name);
}  // namespace sd_skin
