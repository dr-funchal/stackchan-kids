/*
 * SPDX-License-Identifier: MIT
 */
#include "panda.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

// Mouth layout, relative to the screen center (320x240)
static const Vector2i _mouth_pos        = Vector2i(0, 34);
static const Vector2i _mouth_min_offset = Vector2i(-8, -6);
static const Vector2i _mouth_max_offset = Vector2i(8, 6);
static const Vector2i _mouth_min_size   = Vector2i(34, 5);
static const Vector2i _mouth_max_size   = Vector2i(36, 38);
static const int _mouth_min_radius      = 3;
static const int _mouth_max_radius      = 18;
static const int _fang_inset            = 6;

PandaMouth::PandaMouth(lv_obj_t* parent, lv_color_t color, const lv_image_dsc_t* fangImage)
{
    _mouth = std::make_unique<Container>(parent);
    _mouth->setAlign(LV_ALIGN_CENTER);
    _mouth->setBorderWidth(0);
    _mouth->setBgColor(color);
    _mouth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _mouth->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    // Fangs are siblings drawn over the mouth, hanging from its top edge
    if (fangImage) {
        for (auto* fang : {&_left_fang, &_right_fang}) {
            *fang = std::make_unique<Image>(parent);
            (*fang)->setSrc(fangImage);
            (*fang)->setAlign(LV_ALIGN_CENTER);
        }
    }

    setPosition(_position);
    setWeight(0);
    setRotation(0);
}

PandaMouth::~PandaMouth()
{
    _right_fang.reset();
    _left_fang.reset();
    _mouth.reset();
}

void PandaMouth::update_fangs()
{
    if (!_left_fang) {
        return;
    }

    // Overlap the mouth's top edge by 1px so the fangs look attached
    int fang_h = _left_fang->getHeight();
    int top_y  = _mouth_center.y - _mouth_size.y / 2;
    int fang_y = top_y + fang_h / 2 - 1;
    int fang_x = _mouth_size.x / 2 - _fang_inset;

    _left_fang->setPos(_mouth_center.x - fang_x, fang_y);
    _right_fang->setPos(_mouth_center.x + fang_x, fang_y);
}

void PandaMouth::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _mouth_pos.x + map_range(_position.x, -100, 100, _mouth_min_offset.x, _mouth_max_offset.x);
    auto pos_y = _mouth_pos.y + map_range(_position.y, -100, 100, _mouth_min_offset.y, _mouth_max_offset.y);

    _mouth->setPos(pos_x, pos_y);
    _mouth_center = Vector2i(pos_x, pos_y);
    update_fangs();
}

void PandaMouth::setWeight(int weight)
{
    Feature::setWeight(weight);

    auto size_x = map_range(_weight, 0, 100, _mouth_min_size.x, _mouth_max_size.x);
    auto size_y = map_range(_weight, 0, 100, _mouth_min_size.y, _mouth_max_size.y);
    auto radius = map_range(_weight, 0, 100, _mouth_min_radius, _mouth_max_radius);

    _mouth->setSize(size_x, size_y);
    _mouth->setRadius(radius);
    _mouth_size = Vector2i(size_x, size_y);
    update_fangs();
}

void PandaMouth::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _mouth->setTransformPivot(_mouth->getWidth() / 2, _mouth->getHeight() / 2);
    _mouth->setRotation(rotation);
}

void PandaMouth::setVisible(bool visible)
{
    Element::setVisible(visible);

    _mouth->setHidden(!visible);
    if (_left_fang) {
        _left_fang->setHidden(!visible);
        _right_fang->setHidden(!visible);
    }
}
