/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stackchan_display.h"
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <vector>
#include <atomic>
#include <cstring>
#include <src/misc/cache/lv_cache.h>
#include <settings.h>
#include <lvgl.h>
#include <lvgl_theme.h>
#include <stackchan/stackchan.h>
#include <assets/lang_config.h>
#include <hal/hal.h>
#include <hal/utils/sd_features.h>
#include <hal/utils/papa_letras.h>
#include <hal/utils/panda_mandou.h>
#include <hal/utils/poker.h>
#include <stackchan/inner_state/inner_state.h>
#include <application.h>
#include <stackchan/avatar/skins/sd/sd_skin.h>

using namespace stackchan;
using namespace stackchan::avatar;

#define TAG "StackChanAvatarDisplay"

// Nap mode: doze off after this long without any interaction (touch, head pet, conversation)
static constexpr uint32_t kNapAfterMs     = 5 * 60 * 1000;
static constexpr uint32_t kNapCheckMs     = 250;
static constexpr uint8_t kNapBrightness   = 10;
// Between story pages the robot drops to listening for a few seconds while the AI fetches the next page. The scenery
// stays still through such short gaps and only wakes up when nobody speaks for this long (or the chat ends).
static constexpr uint32_t kSceneryThawMs  = 8000;
static std::atomic<uint32_t> _last_activity_ms{0};
static std::atomic<bool> _nap_requested{false};
static std::atomic<bool> _power_button_pressed{false};
static bool _button_nap_pending = false;  // LVGL task only

void power_button_short_press()
{
    _power_button_pressed.store(true);
}

static void poke_activity()
{
    _last_activity_ms.store(GetHAL().millis());
}

// After a conversation ends (goodbye), touches don't start a new one for a while: kids keep handling the robot
// after "tchau", and every brief touch was waking it up again. The wake word always works
static constexpr uint32_t kTouchStartCooldownMs = 30 * 1000;
static std::atomic<uint32_t> _conversation_ended_ms{0};

static bool touch_start_allowed(const char* source)
{
    uint32_t ended = _conversation_ended_ms.load();
    if (ended != 0 && GetHAL().millis() - ended < kTouchStartCooldownMs) {
        ESP_LOGI(TAG, "Ignoring %s: conversation just ended", source);
        return false;
    }
    ESP_LOGI(TAG, "Conversation started by %s", source);
    return true;
}

// "You can talk now": a thin green frame around the screen while listening. Top layer, so it sits over any face;
// static, so it costs nothing to redraw. Caller holds the LVGL lock
static void set_listening_frame(bool visible)
{
    static lv_obj_t* frame = nullptr;
    if (!frame) {
        frame = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(frame);
        lv_obj_set_size(frame, 320, 240);
        lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_border_width(frame, 5, 0);
        lv_obj_set_style_border_color(frame, lv_color_hex(0x3EC300), 0);
        lv_obj_set_style_border_opa(frame, LV_OPA_80, 0);
        lv_obj_set_style_radius(frame, 12, 0);
        lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (visible) {
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
    }
}

// Body language: one gesture at a time, and not on every sentence
static constexpr uint32_t kGestureCooldownMs = 2500;

static void play_gesture(GestureModifier::Kind kind)
{
    static uint32_t last_gesture_tick = 0;
    uint32_t now                      = GetHAL().millis();
    if (GestureModifier::isActive() || now - last_gesture_tick < kGestureCooldownMs || sd_story::isPlaying()) {
        return;
    }
    last_gesture_tick = now;
    GetStackChan().addModifier(std::make_unique<GestureModifier>(kind));
}

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

// Have to register themes, so the asset apply can update the text font
void StackChanAvatarDisplay::InitializeLcdThemes()
{
    auto text_font       = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font       = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));        // rgb(255, 255, 255)
    light_theme->set_text_color(lv_color_hex(0x000000));              // rgb(0, 0, 0)
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));   // rgb(224, 224, 224)
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));       // rgb(0, 128, 0)
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));  // rgb(221, 221, 221)
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));     // rgb(255, 255, 255)
    light_theme->set_system_text_color(lv_color_hex(0x000000));       // rgb(0, 0, 0)
    light_theme->set_border_color(lv_color_hex(0x000000));            // rgb(0, 0, 0)
    light_theme->set_low_battery_color(lv_color_hex(0x000000));       // rgb(0, 0, 0)
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));        // rgb(0, 0, 0)
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));              // rgb(255, 255, 255)
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));   // rgb(31, 31, 31)
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));       // rgb(0, 128, 0)
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));  // rgb(34, 34, 34)
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));     // rgb(0, 0, 0)
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));       // rgb(255, 255, 255)
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));            // rgb(255, 255, 255)
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));       // rgb(255, 0, 0)
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

StackChanAvatarDisplay::StackChanAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                                               bool mirror_y, bool swap_xy)
    : LvglDisplay(), panel_io_(panel_io), panel_(panel)
{
    width_  = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_         = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Draw white screen
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // port_cfg.task_priority   = 20;
    port_cfg.task_priority = 3;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle      = panel_io_,
        .panel_handle   = panel_,
        .control_handle = nullptr,
        .buffer_size    = static_cast<uint32_t>(width_ * 20),
        .double_buffer  = false,
        .trans_size     = 0,
        .hres           = static_cast<uint32_t>(width_),
        .vres           = static_cast<uint32_t>(height_),
        .monochrome     = false,
        .rotation =
            {
                .swap_xy  = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma     = 1,
                .buff_spiram  = 0,
                .sw_rotate    = 0,
                .swap_bytes   = 1,
                .full_refresh = 0,
                .direct_mode  = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->SetPreviewImage(nullptr);
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);

    // Create boot logo label if not warm boot
    if (GetHAL().getWarmRebootTarget() < 0) {
        ESP_LOGI(TAG, "Create boot logo label");
        Lock();
        {
            uitk::lvgl_cpp::ScreenActive screen;
            screen.setBgColor(lv_color_hex(0x000000));
        }
        GetHAL().bootLogo = std::make_unique<BootLogo>();
        Unlock();
    }

    // Robot will be created later in SetupXiaoZhiUI()
}

StackChanAvatarDisplay::~StackChanAvatarDisplay()
{
    ESP_LOGI(TAG, "Destroying StackChanAvatarDisplay");

    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }

    auto& stackchan = GetStackChan();
    if (stackchan.hasAvatar()) {
        stackchan.resetAvatar();
    }
}

bool StackChanAvatarDisplay::Lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void StackChanAvatarDisplay::Unlock()
{
    lvgl_port_unlock();
}

lv_disp_t* StackChanAvatarDisplay::GetLvglDisplay()
{
    return display_;
}

#include <hal/board/hal_bridge.h>

void StackChanAvatarDisplay::SetupUI()
{
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called

    auto& stackchan = GetStackChan();

    if (stackchan.hasAvatar()) {
        ESP_LOGW(TAG, "Avatar already created");
        return;
    }

    DisplayLockGuard lock(this);

    ESP_LOGI(TAG, "Creating Stack-chan Avatar...");

    // Face: a skin chosen on the SD card, else the built-in one
    std::unique_ptr<Avatar> avatar;
    uitk::lvgl_cpp::Container* panel = nullptr;
    auto skin_dir                    = sd_skin::activeSkinDir();
    if (!skin_dir.empty()) {
        auto sd_avatar = std::make_unique<SdSkinAvatar>();
        if (sd_avatar->load(skin_dir)) {
            sd_avatar->init(lv_screen_active());
            panel  = sd_avatar->getPanel();
            avatar = std::move(sd_avatar);
        }
    }
    if (!avatar) {
        auto builtin = std::make_unique<CurrentAvatar>();
        builtin->init(lv_screen_active());
        panel  = builtin->getPanel();
        avatar = std::move(builtin);
    }
    panel->onClick().connect([]() {
        poke_activity();
        inner_state::onEvent(inner_state::Event::Touch);
        if (panda_mandou::onScreenTap()) {
            return;  // "Touch my face" command in the game
        }

        static uint32_t last_toggle_tick = 0;
        const uint32_t now               = GetHAL().millis();
        if (last_toggle_tick != 0 && now - last_toggle_tick < 2000) {
            return;
        }

        if (hal_bridge::is_xiaozhi_ready()) {
            if (hal_bridge::is_xiaozhi_idle() && !touch_start_allowed("screen tap")) {
                return;
            }
            last_toggle_tick = now;
            hal_bridge::toggle_xiaozhi_chat_state();
        }
    });

    stackchan.attachAvatar(std::move(avatar));
    stackchan.addModifier(std::make_unique<BreathModifier>());
    blink_modifier_id_ = stackchan.addModifier(std::make_unique<BlinkModifier>());
    stackchan.addModifier(std::make_unique<HeadPetModifier>());
    stackchan.addModifier(std::make_unique<ImuEventModifier>());
    GetHAL().onImuMotionEvent.connect([](ImuMotionEvent event) {
        if (event == ImuMotionEvent::Shake) {
            inner_state::onEvent(inner_state::Event::Shake);
        }
    });

    // Nap mode: any head pet counts as interaction; the check runs in the LVGL task
    poke_activity();
    // A quick tap on the head (press and release, no swipe) starts a conversation, like tapping the screen.
    // Petting (swipes) keeps its hearts and does not start listening
    GetHAL().onHeadPetGesture.connect([](HeadPetGesture gesture) {
        static uint32_t press_tick    = 0;
        static bool swiped            = false;
        static uint32_t last_chat_tick = 0;
        poke_activity();

        uint32_t now = GetHAL().millis();
        if (gesture == HeadPetGesture::Press) {
            press_tick = now;
            swiped     = false;
        } else if (gesture == HeadPetGesture::SwipeForward || gesture == HeadPetGesture::SwipeBackward) {
            swiped = true;
        } else if (gesture == HeadPetGesture::Release) {
            bool is_tap = !swiped && press_tick != 0 && now - press_tick < 700;
            press_tick  = 0;
            inner_state::onEvent(swiped ? inner_state::Event::HeadPet : inner_state::Event::Touch);
            // During Papa-Letras: tap = hint, petting = skip
            if (papa_letras::isActive() && now - last_chat_tick > 2000) {
                last_chat_tick = now;
                if (is_tap) {
                    papa_letras::onHeadTap();
                } else if (swiped) {
                    papa_letras::onHeadPet();
                }
                return;
            }
            if (is_tap && hal_bridge::is_xiaozhi_ready() && hal_bridge::is_xiaozhi_idle() &&
                now - last_chat_tick > 2000 && touch_start_allowed("head tap")) {
                last_chat_tick = now;
                hal_bridge::toggle_xiaozhi_chat_state();
            }
        }
    });
    // Stories: lip sync on the story audio, scenery held still so the decoder gets the CPU
    sd_story::setPlaybackHook([](bool playing) {
        static int story_lip_sync_id = -1;
        LvglLockGuard lock;
        auto& stackchan = GetStackChan();
        if (playing && story_lip_sync_id < 0) {
            story_lip_sync_id = stackchan.addModifier(std::make_unique<LipSyncModifier>());
        } else if (!playing && story_lip_sync_id >= 0) {
            stackchan.removeModifier(story_lip_sync_id);
            story_lip_sync_id = -1;
            if (stackchan.hasAvatar()) {
                stackchan.avatar().mouth().setWeight(0);
            }
        }
        setSceneryFrozen(playing);
    });

    nap_timer_ = lv_timer_create(
        [](lv_timer_t* timer) { static_cast<StackChanAvatarDisplay*>(lv_timer_get_user_data(timer))->NapCheck(); },
        kNapCheckMs, this);

    preview_image_ = lv_image_create(lv_screen_active());
    lv_obj_set_size(preview_image_, 320, 240);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    // GetHAL().startStackChanAutoUpdate(24);

    auto config        = hal_bridge::get_xiaozhi_config();
    idle_motion_level_ = config.idleRandomMovementLevel;

    ESP_LOGI(TAG, "Avatar created and started");
}

void StackChanAvatarDisplay::LvglLock()
{
    if (!Lock(30000)) {
        ESP_LOGE("Display", "Failed to lock display");
    }
}

void StackChanAvatarDisplay::LvglUnlock()
{
    Unlock();
}

void StackChanAvatarDisplay::CreateIdleMotionModifier()
{
    auto& stackchan = GetStackChan();

    switch (idle_motion_level_) {
        case 0:
            idle_motion_modifier_id_ = -1;
            return;
        case 1:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>(8000, 12000));
            return;
        case 3:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>(2000, 4000));
            return;
        case 2:
        default:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>());
            return;
    }
}

void StackChanAvatarDisplay::SetEmotion(const char* emotion)
{
    auto& stackchan = GetStackChan();

    if (!stackchan.hasAvatar() || !emotion) {
        return;
    }

    DisplayLockGuard lock(this);

    // ESP_LOGE(TAG, "SetEmotion: %s", emotion);

    auto& avatar = stackchan.avatar();

    // Map emotion string to stackchan::Emotion
    auto is = [emotion](std::initializer_list<const char*> names) {
        for (auto* name : names) {
            if (strcmp(emotion, name) == 0) {
                return true;
            }
        }
        return false;
    };

    // Face expression plus body language (head choreography and body LEDs)
    if (is({"neutral"})) {
        avatar.setEmotion(Emotion::Neutral);
    } else if (is({"happy", "laughing", "funny", "silly", "delicious"})) {
        avatar.setEmotion(Emotion::Happy);
        play_gesture(GestureModifier::Kind::Happy);
    } else if (is({"angry"})) {
        avatar.setEmotion(Emotion::Angry);
        play_gesture(GestureModifier::Kind::Angry);
    } else if (is({"sad", "crying"})) {
        avatar.setEmotion(Emotion::Sad);
        play_gesture(GestureModifier::Kind::Sad);
    } else if (is({"surprised", "shocked"})) {
        avatar.setEmotion(Emotion::Neutral);
        play_gesture(GestureModifier::Kind::Surprised);
    } else if (is({"thinking", "confused"})) {
        avatar.setEmotion(Emotion::Doubt);
        play_gesture(GestureModifier::Kind::Thinking);
    } else if (is({"loving", "kissy", "embarrassed"})) {
        avatar.setEmotion(Emotion::Happy);
        play_gesture(GestureModifier::Kind::Love);
    } else if (is({"cool", "confident", "relaxed"})) {
        avatar.setEmotion(Emotion::Neutral);
        play_gesture(GestureModifier::Kind::Cool);
    } else if (is({"winking"})) {
        avatar.setEmotion(Emotion::Happy);
        play_gesture(GestureModifier::Kind::Nod);
    } else if (is({"sleepy"})) {
        avatar.setEmotion(Emotion::Sleepy);
        avatar.setSpeech("Zzz…");
        is_sleeping_ = true;
        // avatar.mouth().setWeight(10);

        // Stop idle motion
        ESP_LOGW(TAG, "Stop idle motion");
        if (idle_motion_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_motion_modifier_id_);
            idle_motion_modifier_id_ = -1;
            stackchan.removeModifier(idle_expression_modifier_id_);
            idle_expression_modifier_id_ = -1;
        }

        // Return to default pose
        auto& motion = GetStackChan().motion();
        motion.pitchServo().moveWithSpeed(0, 80);

    } else if (strcmp(emotion, "doubtful") == 0) {
        avatar.setEmotion(Emotion::Doubt);
    } else {
        ESP_LOGW(TAG, "Unknown emotion: %s, using NEUTRAL", emotion);
        avatar.setEmotion(Emotion::Neutral);
    }

    // Resync blink modifier base eye weights
    auto blink_modifier = static_cast<BlinkModifier*>(stackchan.getModifier(blink_modifier_id_));
    if (blink_modifier) {
        blink_modifier->resyncEyeWeights();
    }
}

void StackChanAvatarDisplay::SetChatMessage(const char* role, const char* content)
{
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!", role, content);
    }

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    // ESP_LOGE(TAG, "SetChatMessage: role=%s, content=%s", role ? role : "null", content ? content : "null");

    DisplayLockGuard lock(this);

    if (strcmp(role, "user") == 0) {
        sd_diary::log("crianca", content);
    } else if (strcmp(role, "assistant") == 0) {
        sd_diary::log("stackchan", content);
    }

    if (strcmp(role, "system") == 0) {
        stackchan.avatar().setSpeech(content);
    } else if (strcmp(role, "assistant") == 0) {
        stackchan.avatar().setSpeech(content);
    }
}

void StackChanAvatarDisplay::ClearChatMessages()
{
    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    DisplayLockGuard lock(this);

    stackchan.avatar().clearSpeech();

    ESP_LOGI(TAG, "Chat messages cleared");
}

void StackChanAvatarDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image)
{
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc          = preview_image_cached_->image_dsc();
    // Set image source and show preview image
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fit width
        lv_image_set_scale(preview_image_, 256 * width_ / img_dsc->header.w);
    }

    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(preview_image_);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, 6000 * 1000));
}

void StackChanAvatarDisplay::UpdateStatusBar(bool update_all)
{
}

void StackChanAvatarDisplay::SetTheme(Theme* theme)
{
    ESP_LOGI(TAG, "SetTheme: %s", theme->name().c_str());

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        ESP_LOGE(TAG, "Avatar is invalid");
        return;
    }

    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font  = lvgl_theme->text_font()->font();

    stackchan.avatar().setSpeechTextFont((void*)text_font);
}

#include <hal/board/hal_bridge.h>
static bool _is_xiaozhi_ready = false;
static bool _is_xiaozhi_idle  = false;
bool hal_bridge::is_xiaozhi_ready()
{
    return _is_xiaozhi_ready;
}
bool hal_bridge::is_xiaozhi_idle()
{
    return _is_xiaozhi_idle;
}

// LVGL task only (called under the display lock)
void StackChanAvatarDisplay::ThawSceneryLater()
{
    if (scenery_thaw_timer_) {
        lv_timer_reset(scenery_thaw_timer_);
        return;
    }
    scenery_thaw_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self                = static_cast<StackChanAvatarDisplay*>(lv_timer_get_user_data(timer));
            self->scenery_thaw_timer_ = nullptr;
            lv_timer_delete(timer);
            if (!sd_story::isPlaying()) {
                setSceneryFrozen(false);
            }
        },
        kSceneryThawMs, this);
}

void StackChanAvatarDisplay::CancelSceneryThaw()
{
    if (scenery_thaw_timer_) {
        lv_timer_delete(scenery_thaw_timer_);
        scenery_thaw_timer_ = nullptr;
    }
}

void StackChanAvatarDisplay::SetStatus(const char* status)
{
    // ESP_LOGE(TAG, "SetStatus: %s", status);

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        ESP_LOGE(TAG, "Avatar is invalid");
        return;
    }

    auto& avatar = stackchan.avatar();
    auto& motion = stackchan.motion();

    DisplayLockGuard lock(this);

    // Every state change (wake word, listening, speaking, back to standby) counts as interaction
    poke_activity();
    const bool is_standby_status   = strcmp(status, Lang::Strings::STANDBY) == 0;
    const bool is_listening_status = strcmp(status, Lang::Strings::LISTENING) == 0;
    if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        CancelSceneryThaw();
    } else if (!sd_story::isPlaying()) {
        if (is_listening_status) {
            ThawSceneryLater();
        } else {
            CancelSceneryThaw();
            setSceneryFrozen(false);
        }
    }
    set_listening_frame(is_listening_status);
    if (is_listening_status && _is_xiaozhi_idle) {
        inner_state::onEvent(inner_state::Event::ConversationStart);  // From standby straight to listening
    }
    papa_letras::onListening(is_listening_status);
    if (is_listening_status) {
        sd_recorder::onListening();
        // Green body LEDs while leaning in = "you can talk now". No sound here: playing a sound right as listening
        // starts (mic, AEC and wake word being switched on) corrupted the heap even when deferred (see CLAUDE.md)
        if (!papa_letras::isActive() && !panda_mandou::isActive()) {
            play_gesture(GestureModifier::Kind::Listen);  // Lean in: "I'm listening" (the game's turn clock owns the LEDs)
        }
    }
    if (is_standby_status) {
        sd_web::start();
        if (!_is_xiaozhi_idle && _is_xiaozhi_ready) {
            _conversation_ended_ms.store(GetHAL().millis());  // A conversation just ended (goodbye or timeout)
            // A game the AI never closed (no "end" call) would leave its cards or letter on screen
            papa_letras::onConversationEnded();
            poker::onConversationEnded();
        }
    }
    sd_story::onDeviceStatus(is_standby_status, is_listening_status);

    bool is_idle      = false;
    bool is_listening = false;

    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        if (speaking_modifier_id_ >= 0) {
            // Start speaking
            stackchan.removeModifier(speaking_modifier_id_);
            avatar.mouth().setWeight(0);
            speaking_modifier_id_ = -1;
        }

        GetHAL().setRgbColor(0, 0, 50, 0);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        _is_xiaozhi_ready = true;

        if (speaking_modifier_id_ >= 0) {
            // Stop speaking
            stackchan.removeModifier(speaking_modifier_id_);
            avatar.mouth().setWeight(0);
            speaking_modifier_id_ = -1;
        }

        is_idle = true;

        GetHAL().setRgbColor(0, 0, 0, 0);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        // Leave the CPU to the audio decoder while speaking (every time: speech can resume without a listening gap)
        setSceneryFrozen(true);
        if (speaking_modifier_id_ < 0) {
            speaking_modifier_id_ = stackchan.addModifier(std::make_unique<LipSyncModifier>());
        }

        GetHAL().setRgbColor(0, 0, 0, 50);
        GetHAL().refreshRgb();
    } else {
        avatar.setSpeech(status);
    }

    if (is_idle) {
        // Start idle motion
        ESP_LOGW(TAG, "Start idle motion");
        StartIdleBehaviors();

        _is_xiaozhi_idle = true;
    } else {
        // Stop idle motion
        ESP_LOGW(TAG, "Stop idle motion");
        StopIdleBehaviors();

        // if (!is_listening) {
        //     // Return to default pose
        //     motion.pitchServo().moveWithSpeed(200, 350);
        //     motion.yawServo().moveWithSpeed(0, 350);
        // }

        _is_xiaozhi_idle = false;
    }

    // Clear sleep state
    if (is_sleeping_) {
        avatar.setSpeech("");
    }
}

void StackChanAvatarDisplay::ShowNotification(const char* notification, int duration_ms)
{
}

void StackChanAvatarDisplay::StartIdleBehaviors()
{
    auto& stackchan = GetStackChan();

    if (idle_motion_modifier_id_ < 0 && idle_motion_level_ > 0) {
        CreateIdleMotionModifier();
    }
    if (idle_expression_modifier_id_ < 0) {
        idle_expression_modifier_id_ = stackchan.addModifier(std::make_unique<IdleExpressionModifier>());
    }
}

void StackChanAvatarDisplay::StopIdleBehaviors()
{
    auto& stackchan = GetStackChan();

    if (idle_motion_modifier_id_ >= 0) {
        stackchan.removeModifier(idle_motion_modifier_id_);
        idle_motion_modifier_id_ = -1;
    }
    if (idle_expression_modifier_id_ >= 0) {
        stackchan.removeModifier(idle_expression_modifier_id_);
        idle_expression_modifier_id_ = -1;
    }
}

// Called by the battery power save timer (esp_timer task): just hand it over to the nap check
void StackChanAvatarDisplay::SetPowerSaveMode(bool on)
{
    if (on) {
        _nap_requested.store(true);
    } else {
        poke_activity();
    }
}

// Runs in the LVGL task (LVGL lock held)
// A speech whose audio never arrives left the robot "speaking" forever: with internal RAM nearly gone, Wi-Fi dropped
// the packets mid-sentence (poker, 2026-10-09). Ask the server to stop, then fall back to standby so the children can
// wake the robot up again. Legit pauses while the AI runs a tool last a few seconds; these limits are well above.
static constexpr uint32_t kStalledSpeechAbortMs   = 20000;
static constexpr uint32_t kStalledSpeechStandbyMs = 30000;

static void check_stalled_speech()  // LVGL task, from NapCheck
{
    static uint32_t quiet_since = 0;
    static int stage            = 0;
    auto& app                   = Application::GetInstance();
    uint32_t now                = GetHAL().millis();
    if (app.GetDeviceState() != kDeviceStateSpeaking || !app.GetAudioService().IsIdle()) {
        quiet_since = now;
        stage       = 0;
        return;
    }
    uint32_t quiet = now - quiet_since;
    if (stage == 0 && quiet >= kStalledSpeechAbortMs) {
        stage = 1;
        ESP_LOGW(TAG, "Speech stalled for %lu ms: aborting", (unsigned long)quiet);
        app.Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.AbortSpeaking(kAbortReasonNone);
            }
        });
    } else if (stage == 1 && quiet >= kStalledSpeechStandbyMs) {
        stage = 2;
        ESP_LOGW(TAG, "Speech still stalled: back to standby");
        app.Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.SetDeviceState(kDeviceStateIdle);
            }
        });
    }
}

void StackChanAvatarDisplay::NapCheck()
{
    check_stalled_speech();
    inner_state::tick(is_napping_, _is_xiaozhi_ready && !_is_xiaozhi_idle);

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    // Side button: wake up if napping; otherwise end any conversation and nap as soon as it is idle
    if (_power_button_pressed.exchange(false)) {
        if (is_napping_) {
            ESP_LOGI(TAG, "Power button: wake up");
            poke_activity();
        } else {
            ESP_LOGI(TAG, "Power button: go to sleep");
            _button_nap_pending = true;
            sd_story::stop();
            Application::GetInstance().Schedule([]() {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateSpeaking) {
                    app.AbortSpeaking(kAbortReasonNone);
                }
                // After the abort settles, close the conversation if it went back to listening
                app.Schedule([]() {
                    auto& app = Application::GetInstance();
                    if (app.GetDeviceState() == kDeviceStateListening) {
                        app.ToggleChatState();
                    }
                });
            });
        }
    }
    if (_button_nap_pending && !is_napping_ && _is_xiaozhi_idle) {
        _button_nap_pending = false;
        EnterNap();
        return;
    }

    // A story counts as activity: don't doze off in the middle of it
    if (sd_story::isPlaying()) {
        poke_activity();
    }

    uint32_t now           = GetHAL().millis();
    uint32_t last_activity = _last_activity_ms.load();

    if (is_napping_) {
        if ((int32_t)(last_activity - nap_started_ms_) > 0) {
            ExitNap();
        }
        return;
    }

    bool nap_requested = _nap_requested.exchange(false);
    if (_is_xiaozhi_idle && (nap_requested || now - last_activity >= kNapAfterMs)) {
        EnterNap();
    }
}

void StackChanAvatarDisplay::EnterNap()
{
    ESP_LOGI(TAG, "No interaction for a while, taking a nap");

    auto& stackchan = GetStackChan();
    auto& avatar    = stackchan.avatar();

    is_napping_     = true;
    nap_started_ms_ = GetHAL().millis();

    // Stop moving: no idle motion or expressions, scenery frozen, head lowered
    StopIdleBehaviors();
    setSceneryPaused(true);
    stackchan.motion().pitchServo().moveWithSpeed(0, 80);

    // Eyes fully closed; resync so blinking keeps them closed
    avatar.setEmotion(Emotion::Sleepy);
    avatar.leftEye().setWeight(0);
    avatar.rightEye().setWeight(0);
    avatar.mouth().setWeight(0);
    avatar.setSpeech("Zzz…");
    auto blink_modifier = static_cast<BlinkModifier*>(stackchan.getModifier(blink_modifier_id_));
    if (blink_modifier) {
        blink_modifier->resyncEyeWeights();
    }

    brightness_before_nap_ = GetHAL().getBackLightBrightness();
    GetHAL().setBackLightBrightness(kNapBrightness);
}

void StackChanAvatarDisplay::ExitNap()
{
    ESP_LOGI(TAG, "Woke up from nap");

    auto& stackchan = GetStackChan();
    auto& avatar    = stackchan.avatar();

    is_napping_ = false;

    // Never wake up to a dark screen if the brightness read before the nap was bogus
    GetHAL().setBackLightBrightness(brightness_before_nap_ > kNapBrightness ? brightness_before_nap_ : 75);

    avatar.setEmotion(Emotion::Neutral);
    avatar.setSpeech("");
    auto blink_modifier = static_cast<BlinkModifier*>(stackchan.getModifier(blink_modifier_id_));
    if (blink_modifier) {
        blink_modifier->resyncEyeWeights();
    }

    setSceneryPaused(false);
    if (_is_xiaozhi_idle) {
        StartIdleBehaviors();
    }
}
