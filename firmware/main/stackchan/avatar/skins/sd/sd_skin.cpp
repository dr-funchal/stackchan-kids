/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_skin.h"
#include "../default/default.h"
#include "../panda/panda.h"
#include <hal/utils/sd_card.h>
#include <hal/utils/sd_features.h>
#include <hal/utils/sd_images.h>
#include <ArduinoJson.hpp>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <cstdio>

using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const char* TAG = "SdSkin";


// The skin lives for the whole run; its PNG bytes stay in PSRAM for LVGL's PNG decoder
static uint8_t* _png_data = nullptr;
static lv_image_dsc_t _png_dsc;

static std::string active_file()
{
    return std::string(sd_paths::kSkins) + "/ativa.txt";
}

static bool parse_color(const char* text, lv_color_t& out)
{
    if (!text || text[0] != '#' || strlen(text) != 7) {
        return false;
    }
    out = lv_color_hex(strtoul(text + 1, nullptr, 16));
    return true;
}

static std::string read_file(const std::string& path, size_t maxSize)
{
    std::string content;
    sd_card::BusGuard guard;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return content;
    }
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && content.size() < maxSize) {
        content.append(buf, n);
    }
    fclose(f);
    return content;
}

bool SdSkinAvatar::load(const std::string& dir)
{
    std::string png_path = dir + "/fundo.png";
    sd_card::BusGuard guard;  // Runs once at boot, before the face is drawn
    struct stat st       = {};
    if (stat(png_path.c_str(), &st) != 0 || st.st_size < 33 || st.st_size > 1024 * 1024) {
        ESP_LOGW(TAG, "Missing or too large: %s", png_path.c_str());
        return false;
    }

    if (_png_data) {
        heap_caps_free(_png_data);
        _png_data = nullptr;
    }
    _png_data = (uint8_t*)heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM);
    FILE* f   = fopen(png_path.c_str(), "rb");
    if (!_png_data || !f || fread(_png_data, 1, st.st_size, f) != (size_t)st.st_size) {
        if (f) {
            fclose(f);
        }
        ESP_LOGW(TAG, "Cannot read %s", png_path.c_str());
        return false;
    }
    fclose(f);

    static const uint8_t png_magic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(_png_data, png_magic, 8) != 0) {
        ESP_LOGW(TAG, "%s is not a PNG", png_path.c_str());
        return false;
    }

    // Width and height sit big-endian in the IHDR chunk
    auto be32 = [](const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
    _png_dsc              = {};
    _png_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    _png_dsc.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
    _png_dsc.header.w     = be32(_png_data + 16);
    _png_dsc.header.h     = be32(_png_data + 20);
    _png_dsc.data         = _png_data;
    _png_dsc.data_size    = st.st_size;

    std::string json = read_file(dir + "/skin.json", 2048);
    if (!json.empty()) {
        ArduinoJson::JsonDocument doc;
        if (ArduinoJson::deserializeJson(doc, json) == ArduinoJson::DeserializationError::Ok) {
            parse_color(doc["olhos"] | "", _eye_color);
            parse_color(doc["palpebra"] | "", _eyelid_color);
            parse_color(doc["boca"] | "", _mouth_color);
            _fangs = doc["presas"] | false;
        } else {
            ESP_LOGW(TAG, "Ignoring invalid %s/skin.json", dir.c_str());
        }
    }

    ESP_LOGI(TAG, "Loaded skin %s (%ux%u)", dir.c_str(), (unsigned)_png_dsc.header.w, (unsigned)_png_dsc.header.h);
    return true;
}

void SdSkinAvatar::init(lv_obj_t* parent, const lv_font_t* font)
{
    _pannel = std::make_unique<Container>(parent);
    _pannel->align(LV_ALIGN_CENTER, 0, 0);
    _pannel->setSize(320, 240);
    _pannel->setRadius(0);
    _pannel->setBorderWidth(0);
    _pannel->setPadding(0, 0, 0, 0);
    _pannel->setBgColor(lv_color_black());
    _pannel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _background = std::make_unique<Image>(_pannel->get());
    _background->setSrc(&_png_dsc);
    _background->setAlign(LV_ALIGN_TOP_LEFT);
    _background->setPos(0, 0);

    _key_elements.leftEye  = std::make_unique<DefaultEyes>(_pannel->get(), _eye_color, _eyelid_color, true);
    _key_elements.rightEye = std::make_unique<DefaultEyes>(_pannel->get(), _eye_color, _eyelid_color, false);
    _key_elements.mouth = std::make_unique<PandaMouth>(_pannel->get(), _mouth_color, _fangs ? sd_images::get("halloween/halloween_fang") : nullptr);
    (void)font;  // No speech bubble, like the Halloween face
}

Container* SdSkinAvatar::getPanel() const
{
    return _pannel.get();
}

std::string sd_skin::activeSkinDir()
{
    if (!sd_card::isMounted()) {
        return "";
    }
    std::string name = read_file(active_file(), 64);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) {
        name.pop_back();
    }
    if (name.empty() || name == "padrao" || name.find('/') != std::string::npos || name.find("..") != std::string::npos) {
        return "";
    }
    return std::string(sd_paths::kSkins) + "/" + name;
}

std::vector<std::string> sd_skin::list()
{
    return sd_paths::listDirs(sd_paths::kSkins);
}

bool sd_skin::setActive(const std::string& name)
{
    if (!sd_card::isMounted()) {
        return false;
    }
    std::string chosen = "padrao";
    if (!name.empty() && name != "padrao") {
        for (auto& skin : list()) {
            if (skin == name || sd_paths::sanitize(skin) == sd_paths::sanitize(name)) {
                chosen = skin;
            }
        }
        if (chosen == "padrao") {
            return false;
        }
    }
    sd_card::BusGuard guard;
    FILE* f = fopen(active_file().c_str(), "w");
    if (!f) {
        return false;
    }
    fputs(chosen.c_str(), f);
    fclose(f);
    ESP_LOGI(TAG, "Active skin set to %s", chosen.c_str());
    return true;
}

std::string sd_skin::activeIdentity()
{
    std::string dir = activeSkinDir();
    if (dir.empty()) {
        return kCurrentAvatarIdentity;
    }
    {
        // Same test as SdSkinAvatar::load: without its background the display falls back to the built-in face
        sd_card::BusGuard guard;
        struct stat st = {};
        if (stat((dir + "/fundo.png").c_str(), &st) != 0) {
            return kCurrentAvatarIdentity;
        }
    }
    std::string identity;
    std::string json = read_file(dir + "/skin.json", 2048);
    ArduinoJson::JsonDocument doc;
    if (!json.empty() && ArduinoJson::deserializeJson(doc, json) == ArduinoJson::DeserializationError::Ok) {
        identity = doc["identidade"] | "";
    }
    if (identity.empty()) {
        identity = "a robot whose face is the \"" + dir.substr(dir.rfind('/') + 1) + "\" skin";
    }
    return identity;
}
