/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../sd/sd_skin.h"

namespace stackchan::avatar {

/**
 * @brief Little race-car driver: red helmet with hot-rod flames, golden goggles, waving checkered flags.
 * The SD skin face (GlintEyes, SmileMouth, CheckeredFlagDecorator) with its background and skin.json built into
 * the firmware, so it needs no card. Made by assets_src/piloto/build_skin.py (piloto_assets.c)
 */
class PilotAvatar : public SdSkinAvatar {
public:
    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);
};

}  // namespace stackchan::avatar
