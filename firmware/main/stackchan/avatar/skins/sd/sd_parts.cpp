/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_skin.h"
#include "../panda/panda.h"
#include <hal/hal.h>
#include <cmath>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan;
using namespace stackchan::avatar;

namespace {
void plain(Container& obj)
{
    obj.setBorderWidth(0);
    obj.setPadding(0, 0, 0, 0);
    obj.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    obj.removeFlag(LV_OBJ_FLAG_CLICKABLE);
}
}  // namespace

/* -------------------------------------------------------------------------- */
/*                                  GlintEyes                                 */
/* -------------------------------------------------------------------------- */

// Same spots as DefaultEyes (screen center + (-+70, -16)), so the SD backgrounds line up
static const Vector2i _eye_pos        = Vector2i(-70, -16);
static const Vector2i _eye_min_offset = Vector2i(-16, -16);
static const Vector2i _eye_max_offset = Vector2i(16, 16);
static const Vector2i _eye_size_limit = Vector2i(26, 44);
static const int _eye_box             = 48;

GlintEyes::GlintEyes(lv_obj_t* parent, lv_color_t eyeColor, lv_color_t pupilColor, lv_color_t eyelidColor,
                     bool isLeftEye)
{
    _is_left_eye = isLeftEye;

    _container = std::make_unique<Container>(parent);
    plain(*_container);
    _container->setRadius(0);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setBgOpa(0);
    _container->setSize(_eye_box, _eye_box);
    _container->setTransformPivot(_eye_box / 2, _eye_box / 2);

    _eye = std::make_unique<Container>(_container->get());
    plain(*_eye);
    _eye->setRadius(LV_RADIUS_CIRCLE);
    _eye->align(LV_ALIGN_CENTER, 0, 0);
    _eye->setBgColor(eyeColor);

    _pupil = std::make_unique<Container>(_eye->get());
    plain(*_pupil);
    _pupil->setRadius(LV_RADIUS_CIRCLE);
    _pupil->setAlign(LV_ALIGN_CENTER);
    _pupil->setBgColor(pupilColor);

    _glint = std::make_unique<Container>(_eye->get());
    plain(*_glint);
    _glint->setRadius(LV_RADIUS_CIRCLE);
    _glint->setAlign(LV_ALIGN_CENTER);
    _glint->setBgColor(lv_color_white());

    // Drawn last, over the eye; when open it sits above the eye, on the same color as the face around it
    _eyelid = std::make_unique<Container>(_container->get());
    plain(*_eyelid);
    _eyelid->setRadius(0);
    _eyelid->align(LV_ALIGN_CENTER, 0, 0);
    _eyelid->setBgColor(eyelidColor);

    setSize(0);
    setWeight(100);
    setPosition(_position);
    setRotation(0);
}

GlintEyes::~GlintEyes()
{
    _eyelid.reset();
    _glint.reset();
    _pupil.reset();
    _eye.reset();
    _container.reset();
}

void GlintEyes::place_pupil()
{
    // The pupil leans toward where the eye looks, so a glance reads even from across the room
    int pupil_size = _eye_size * 55 / 100;
    int travel     = (_eye_size - pupil_size) / 2 * 6 / 10;
    int px         = map_range(_position.x, -100, 100, -travel, travel);
    int py         = map_range(_position.y, -100, 100, -travel, travel);
    _pupil->setPos(px, py);
    int glint_size = std::max(4, _eye_size * 20 / 100);
    _glint->setSize(glint_size, glint_size);
    _glint->setPos(px - pupil_size / 4, py - pupil_size / 4);
}

void GlintEyes::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _is_left_eye ? _eye_pos.x : -_eye_pos.x;
    pos_x += map_range(_position.x, -100, 100, _eye_min_offset.x, _eye_max_offset.x);
    auto pos_y = _eye_pos.y + map_range(_position.y, -100, 100, _eye_min_offset.y, _eye_max_offset.y);

    _container->setPos(pos_x, pos_y);
    _eyelid->setY(_eyelid_offset_y);
    place_pupil();
}

void GlintEyes::setWeight(int weight)
{
    Feature::setWeight(weight);

    _eyelid_offset_y = -map_range(_weight, 0, 100, 0, _eye_size);
    _eyelid->setY(_eyelid_offset_y);
}

void GlintEyes::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _container->setRotation(rotation);
}

void GlintEyes::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    // Same looks as DefaultEyes
    auto apply_style = [this](int weight, int rotation) {
        setWeight(weight);
        setRotation(_is_left_eye ? rotation : -rotation);
    };

    switch (emotion) {
        case Emotion::Neutral:
            apply_style(100, 0);
            break;
        case Emotion::Happy:
            apply_style(72, 1550);
            break;
        case Emotion::Angry:
            apply_style(70, 450);
            break;
        case Emotion::Sad:
            apply_style(70, -400);
            break;
        case Emotion::Doubt:
            apply_style(75, 0);
            break;
        case Emotion::Sleepy:
            apply_style(35, -50);
            break;
        default:
            break;
    }
}

void GlintEyes::setVisible(bool visible)
{
    Element::setVisible(visible);

    _container->setHidden(!visible);
}

void GlintEyes::setSize(int size)
{
    Feature::setSize(size);

    _eye_size      = map_range(_size, -100, 100, _eye_size_limit.x, _eye_size_limit.y);
    int pupil_size = _eye_size * 55 / 100;
    _eye->setSize(_eye_size, _eye_size);
    _pupil->setSize(pupil_size, pupil_size);
    _eyelid->setSize(_eye_size, _eye_size);

    setWeight(getWeight());
    place_pupil();
}

/* -------------------------------------------------------------------------- */
/*                                 SmileMouth                                 */
/* -------------------------------------------------------------------------- */

// Same spot as PandaMouth (screen center + (0, 34))
static const Vector2i _mouth_pos        = Vector2i(0, 34);
static const Vector2i _mouth_min_offset = Vector2i(-8, -6);
static const Vector2i _mouth_max_offset = Vector2i(8, 6);
static const int _smile_box             = 40;  // The smile is the bottom of a circle this wide
static const int _smile_lift            = 14;  // Circle center above the mouth spot
static const int _open_from_weight      = 12;  // Below this the mouth stays a closed smile
static const Vector2i _open_min_size    = Vector2i(26, 10);
static const Vector2i _open_max_size    = Vector2i(36, 30);

SmileMouth::SmileMouth(lv_obj_t* parent, lv_color_t color, lv_color_t tongueColor)
{
    _smile = std::make_unique<SmileArc>(parent);
    lv_obj_remove_style(_smile->get(), nullptr, LV_PART_KNOB);
    _smile->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _smile->setSize(_smile_box, _smile_box);
    _smile->setAlign(LV_ALIGN_CENTER);
    _smile->setPadding(0, 0, 0, 0);
    _smile->setArcWidth(4);
    _smile->setArcRounded(true);
    _smile->setArcColor(color);
    _smile->setArcOpa(LV_OPA_TRANSP, LV_PART_INDICATOR);

    _open = std::make_unique<Container>(parent);
    plain(*_open);
    _open->setAlign(LV_ALIGN_CENTER);
    _open->setBgColor(color);
    lv_obj_set_style_clip_corner(_open->get(), true, LV_PART_MAIN);

    _teeth = std::make_unique<Container>(_open->get());
    plain(*_teeth);
    _teeth->setRadius(2);
    _teeth->setBgColor(lv_color_white());

    _tongue = std::make_unique<Container>(_open->get());
    plain(*_tongue);
    _tongue->setRadius(LV_RADIUS_CIRCLE);
    _tongue->setBgColor(tongueColor);

    setPosition(_position);
    setWeight(0);
    setRotation(0);
}

SmileMouth::~SmileMouth()
{
    _tongue.reset();
    _teeth.reset();
    _open.reset();
    _smile.reset();
}

void SmileMouth::update_shape()
{
    auto pos_x = _mouth_pos.x + map_range(_position.x, -100, 100, _mouth_min_offset.x, _mouth_max_offset.x);
    auto pos_y = _mouth_pos.y + map_range(_position.y, -100, 100, _mouth_min_offset.y, _mouth_max_offset.y);

    bool open = _weight >= _open_from_weight;
    _smile->setHidden(!_visible || open);
    _open->setHidden(!_visible || !open);

    if (!open) {
        // LVGL angles: 0 is 3 o'clock, growing clockwise. 30..150 is the bottom of the circle: a smile
        if (_sad) {
            lv_arc_set_bg_angles(_smile->get(), 215, 325);
            _smile->setPos(pos_x, pos_y + _smile_lift + 2);
        } else {
            lv_arc_set_bg_angles(_smile->get(), 30, 150);
            _smile->setPos(pos_x, pos_y - _smile_lift);
        }
        return;
    }

    int w = map_range(_weight, _open_from_weight, 100, _open_min_size.x, _open_max_size.x);
    int h = map_range(_weight, _open_from_weight, 100, _open_min_size.y, _open_max_size.y);
    _open->setSize(w, h);
    _open->setRadius(std::min(w, h) / 2);
    _open->setPos(pos_x, pos_y);

    // Upper teeth along the top edge, tongue resting at the bottom; both clipped by the rounded mouth
    _teeth->setSize(w - 8, std::max(3, h / 6));
    _teeth->align(LV_ALIGN_TOP_MID, 0, 0);
    _tongue->setSize(w * 70 / 100, std::max(6, h * 55 / 100));
    _tongue->align(LV_ALIGN_BOTTOM_MID, 0, h / 5);
}

void SmileMouth::setPosition(const Vector2i& position)
{
    Element::setPosition(position);
    update_shape();
}

void SmileMouth::setWeight(int weight)
{
    Feature::setWeight(weight);
    update_shape();
}

void SmileMouth::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _open->setTransformPivot(_open->getWidth() / 2, _open->getHeight() / 2);
    _open->setRotation(rotation);
    _smile->setTransformPivot(_smile_box / 2, _smile_box / 2);
    _smile->setRotation(rotation);
}

void SmileMouth::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }
    _sad = emotion == Emotion::Sad;
    update_shape();
}

void SmileMouth::setVisible(bool visible)
{
    Element::setVisible(visible);
    update_shape();
}

/* -------------------------------------------------------------------------- */
/*                           CheckeredFlagDecorator                           */
/* -------------------------------------------------------------------------- */

static const int _flag_rows          = 4;
static const int _flag_cell          = 6;  // Square cells; the flag is 6x4 of them
static const int _pole_height        = 46;
static const int _pole_width         = 3;
static const float _wave_amplitude   = 3.0f;  // At the free end, in pixels
static const uint32_t _wave_period   = 900;   // ms
static const float _wave_phase_step  = 0.9f;  // Radians between neighbouring columns
static const uint32_t _flag_frame_ms = 50;

CheckeredFlagDecorator::CheckeredFlagDecorator(lv_obj_t* parent, int x, int y, bool mirrored)
    : _x(x), _y(y), _mirrored(mirrored)
{
    // Pole on the left of the flag, or on its right when mirrored
    int flag_w = kColumns * _flag_cell;
    int pole_x = _mirrored ? x + flag_w : x;

    _pole = std::make_unique<Container>(parent);
    plain(*_pole);
    _pole->setAlign(LV_ALIGN_TOP_LEFT);
    _pole->setRadius(1);
    _pole->setBgColor(lv_color_hex(0xE6E6EE));
    _pole->setBorderWidth(1);
    _pole->setBorderColor(lv_color_hex(0x5A5A66));
    _pole->setSize(_pole_width, _pole_height);
    _pole->setPos(pole_x, y);

    _knob = std::make_unique<Container>(parent);
    plain(*_knob);
    _knob->setAlign(LV_ALIGN_TOP_LEFT);
    _knob->setRadius(LV_RADIUS_CIRCLE);
    _knob->setBgColor(lv_color_hex(0xFFCD00));
    _knob->setSize(7, 7);
    _knob->setPos(pole_x + _pole_width / 2 - 3, y - 4);

    for (int c = 0; c < kColumns; c++) {
        auto& column = _columns[c];
        column       = std::make_unique<Container>(parent);
        plain(*column);
        column->setAlign(LV_ALIGN_TOP_LEFT);
        column->setRadius(0);
        column->setSize(_flag_cell, _flag_rows * _flag_cell);
        // Every other column in light grey, as if the cloth folds in the light
        column->setBgColor(c % 2 == 0 ? lv_color_white() : lv_color_hex(0xDADAE2));
        for (int r = (c % 2); r < _flag_rows; r += 2) {
            auto cell = std::make_unique<Container>(column->get());
            plain(*cell);
            cell->setAlign(LV_ALIGN_TOP_LEFT);
            cell->setRadius(0);
            cell->setBgColor(c % 2 == 0 ? lv_color_hex(0x16161C) : lv_color_hex(0x2C2C36));
            cell->setSize(_flag_cell, _flag_cell);
            cell->setPos(0, r * _flag_cell);
            _cells.push_back(std::move(cell));
        }
    }
    _update();
}

CheckeredFlagDecorator::~CheckeredFlagDecorator()
{
    _cells.clear();
    for (auto& column : _columns) {
        column.reset();
    }
    _knob.reset();
    _pole.reset();
}

void CheckeredFlagDecorator::_update()
{
    uint32_t now = GetHAL().millis();
    if (now < _next_tick || isSceneryPaused() || isSceneryFrozen()) {
        return;
    }
    _next_tick = now + _flag_frame_ms;

    float t = 2.0f * (float)M_PI * (float)(now % _wave_period) / (float)_wave_period;
    for (int c = 0; c < kColumns; c++) {
        // c = 0 is the column at the pole: it barely moves, the free end flaps the most
        float reach = (float)(c + 1) / kColumns;
        int dy      = (int)std::lround(_wave_amplitude * reach * std::sin(t - c * _wave_phase_step));
        int col_x   = _mirrored ? _x + (kColumns - 1 - c) * _flag_cell : _x + _pole_width + c * _flag_cell;
        _columns[c]->setPos(col_x, _y + 1 + dy);
    }
}
