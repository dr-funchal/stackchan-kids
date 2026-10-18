/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../../avatar/avatar.h"
#include "../../avatar/decorator.h"
#include "../../avatar/elements/feature.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <string>
#include <vector>

namespace stackchan::avatar {

/**
 * @brief Face loaded from the microSD card: skins/<name>/fundo.png (320x240 background) plus optional
 * skins/<name>/skin.json {"olhos":"#RRGGBB","palpebra":"#RRGGBB","boca":"#RRGGBB","presas":bool,
 * "identidade":"who the robot is with this face, e.g. a panda dressed up as Santa Claus"}.
 * Eyes are drawn at (90,104)/(230,104) and the mouth at (160,154), like the panda faces.
 *
 * Optional extras in skin.json:
 *  - "olhos_estilo":"brilho": bigger eyes with a pupil ("pupila" color) and a glint (GlintEyes)
 *  - "boca_estilo":"sorriso": smile that opens with teeth and tongue ("lingua" color) (SmileMouth)
 *  - "bandeiras":[{"x":12,"y":8,"espelhar":false}]: waving checkered flags, top-left of the pole at (x, y)
 */
class SdSkinAvatar : public Avatar {
public:
    // Read the skin files; call before init(). Returns false if the skin is missing or broken
    bool load(const std::string& dir);
    // Same, from a PNG and skin.json text already in memory (e.g. built into the firmware). png must outlive the avatar
    bool loadFromMemory(const uint8_t* png, size_t size, const std::string& json);
    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);
    uitk::lvgl_cpp::Container* getPanel() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _background;
    lv_image_dsc_t _png_dsc  = {};
    lv_color_t _eye_color    = lv_color_white();
    lv_color_t _eyelid_color = lv_color_black();
    lv_color_t _mouth_color  = lv_color_black();
    lv_color_t _pupil_color  = lv_color_hex(0x1B1B3A);
    lv_color_t _tongue_color = lv_color_hex(0xFF7A8A);
    bool _fangs              = false;
    bool _glint_eyes         = false;
    bool _smile_mouth        = false;
    struct Flag {
        int x;
        int y;
        bool mirrored;
    };
    std::vector<Flag> _flags;
};

/**
 * @brief Round eye with a dark pupil that looks where the eye looks and a white glint, under an eyelid in the
 * color around it. Same placement, blinking and emotions as DefaultEyes, a little bigger
 */
class GlintEyes : public Feature {
public:
    GlintEyes(lv_obj_t* parent, lv_color_t eyeColor, lv_color_t pupilColor, lv_color_t eyelidColor, bool isLeftEye);
    ~GlintEyes();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setEmotion(const Emotion& emotion) override;
    void setVisible(bool visible) override;
    void setSize(int size) override;

private:
    void place_pupil();

    bool _is_left_eye    = false;
    int _eye_size        = 0;
    int _eyelid_offset_y = 0;
    std::unique_ptr<uitk::lvgl_cpp::Container> _container;
    std::unique_ptr<uitk::lvgl_cpp::Container> _eye;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pupil;
    std::unique_ptr<uitk::lvgl_cpp::Container> _glint;
    std::unique_ptr<uitk::lvgl_cpp::Container> _eyelid;
};

class SmileArc : public uitk::lvgl_cpp::Widget<lv_arc_create> {
public:
    using Widget::Widget;
};

/**
 * @brief Curved smile when quiet; while talking it opens into an oval with a row of teeth and a tongue.
 * Turns into a frown when sad
 */
class SmileMouth : public Feature {
public:
    SmileMouth(lv_obj_t* parent, lv_color_t color, lv_color_t tongueColor);
    ~SmileMouth();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setEmotion(const Emotion& emotion) override;
    void setVisible(bool visible) override;

private:
    void update_shape();

    bool _sad = false;
    std::unique_ptr<SmileArc> _smile;
    std::unique_ptr<uitk::lvgl_cpp::Container> _open;
    std::unique_ptr<uitk::lvgl_cpp::Container> _teeth;
    std::unique_ptr<uitk::lvgl_cpp::Container> _tongue;
};

/**
 * @brief Checkered flag on a pole, waving: its columns ride a sine wave that grows away from the pole.
 * Holds still with the rest of the scenery (setSceneryPaused / setSceneryFrozen)
 */
class CheckeredFlagDecorator : public Decorator {
public:
    CheckeredFlagDecorator(lv_obj_t* parent, int x, int y, bool mirrored);
    ~CheckeredFlagDecorator();

    void _update() override;

private:
    static constexpr int kColumns = 6;
    int _x;
    int _y;
    bool _mirrored;
    uint32_t _next_tick = 0;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pole;
    std::unique_ptr<uitk::lvgl_cpp::Container> _knob;
    std::unique_ptr<uitk::lvgl_cpp::Container> _columns[kColumns];
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _cells;
};

}  // namespace stackchan::avatar

namespace sd_skin {
// Folder of the skin chosen in skins/ativa.txt, or "" for the built-in face
std::string activeSkinDir();
std::vector<std::string> list();
// "padrao" (or empty) goes back to the built-in face. Takes effect after a restart
bool setActive(const std::string& name);
/**
 * @brief Who the robot is with the face it boots with, in a few words for the AI: "identidade" from the active
 * skin's skin.json, else the built-in face's (kCurrentAvatarIdentity). Changing the skin restarts the robot,
 * so this follows the face without anyone updating it
 */
std::string activeIdentity();
}  // namespace sd_skin
