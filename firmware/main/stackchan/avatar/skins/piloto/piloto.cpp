/*
 * SPDX-License-Identifier: MIT
 */
#include "piloto.h"
#include <cstddef>
#include <cstdint>

extern "C" {
extern const uint8_t piloto_fundo_png[];
extern const size_t piloto_fundo_png_size;
extern const char piloto_skin_json[];
}

using namespace stackchan::avatar;

void PilotAvatar::init(lv_obj_t* parent, const lv_font_t* font)
{
    loadFromMemory(piloto_fundo_png, piloto_fundo_png_size, piloto_skin_json);
    SdSkinAvatar::init(parent, font);
}
