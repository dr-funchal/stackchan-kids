/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../../avatar/avatar.h"
#include "../../avatar/decorator.h"
#include "../../avatar/elements/feature.h"
#include "../default/default.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>

namespace stackchan::avatar {

/**
 * @brief Panda face: white face, black ears and eye patches, black nose, small round mouth.
 * Reuses DefaultEyes (white eye, black eyelid) and DefaultSpeechBubble
 */
class PandaAvatar : public Avatar {
public:
    lv_color_t furColor   = lv_color_white();
    lv_color_t patchColor = lv_color_black();
    lv_color_t cheekColor = lv_color_hex(0xFFB3C6);

    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);
    uitk::lvgl_cpp::Container* getPanel() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> make_shape(lv_color_t color, int w, int h, int x, int y,
                                                          lv_opa_t opa = LV_OPA_COVER);

    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _left_ear;
    std::unique_ptr<uitk::lvgl_cpp::Container> _right_ear;
    std::unique_ptr<uitk::lvgl_cpp::Container> _left_patch;
    std::unique_ptr<uitk::lvgl_cpp::Container> _right_patch;
    std::unique_ptr<uitk::lvgl_cpp::Container> _left_cheek;
    std::unique_ptr<uitk::lvgl_cpp::Container> _right_cheek;
    std::unique_ptr<uitk::lvgl_cpp::Container> _nose;
};

/**
 * @brief Small round mouth that opens into an "o", optionally with fang images hanging from its top edge
 */
class PandaMouth : public Feature {
public:
    PandaMouth(lv_obj_t* parent, lv_color_t color, const lv_image_dsc_t* fangImage = nullptr);
    ~PandaMouth();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setVisible(bool visible) override;

private:
    void update_fangs();

    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth;
    std::unique_ptr<uitk::lvgl_cpp::Image> _left_fang;
    std::unique_ptr<uitk::lvgl_cpp::Image> _right_fang;
    uitk::Vector2i _mouth_center;
    uitk::Vector2i _mouth_size;
};

/**
 * @brief Halloween vampire panda: illustrated background (night sky, witch hat, cobweb), glowing
 * orange eyes, outlined fangs, and animated bats, pumpkins and spider
 */
class HalloweenPandaAvatar : public Avatar {
public:
    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);
    uitk::lvgl_cpp::Container* getPanel() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _background;
};

/**
 * @brief Moving scenery of the Halloween avatar. Lives in the avatar's decorator pool, so it is
 * updated with the avatar and destroyed with it
 */
class HalloweenSceneryDecorator : public Decorator {
public:
    HalloweenSceneryDecorator(lv_obj_t* parent);
    ~HalloweenSceneryDecorator();

    void _update() override;

private:
    struct Bat {
        std::unique_ptr<uitk::lvgl_cpp::Image> image;
        uitk::Vector2i center;
        uitk::Vector2i radius;
        uint32_t periodMs;
        uint32_t phaseMs;
        bool wingsUp;
    };
    struct Pumpkin {
        std::unique_ptr<uitk::lvgl_cpp::Image> image;
        const lv_image_dsc_t* lit;
        const lv_image_dsc_t* unlit;
        uitk::Vector2i pos;
        uint32_t phaseMs;
        uint32_t unlitUntil;
        bool isLit;
    };

    Bat _bats[3];
    Pumpkin _pumpkins[2];
    std::unique_ptr<uitk::lvgl_cpp::Container> _thread;
    std::unique_ptr<uitk::lvgl_cpp::Image> _spider;
    uint32_t _next_flap_tick = 0;
    bool _paused             = false;
};

/**
 * @brief Freeze or resume the Halloween scenery (no-op for other avatars). Used by the nap mode
 */
void setSceneryPaused(bool paused);

/**
 * @brief Hold the Halloween scenery still (candles stay lit), so speech audio gets the CPU
 */
void setSceneryFrozen(bool frozen);

/**
 * @brief Avatar used by the face screens. Switch back to PandaAvatar (or DefaultAvatar) after October
 */
using CurrentAvatar = HalloweenPandaAvatar;

}  // namespace stackchan::avatar
