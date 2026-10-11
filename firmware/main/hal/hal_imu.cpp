/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/bmi270/bmi270.h"
#include "utils/motion_detector/motion_detector.h"
#include <mooncake_log.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>

static const std::string_view _tag = "HAL-IMU";

static std::unique_ptr<BMI270> _bmi270;

// Smoothed gravity now and at boot, for the tilt the robot can feel (inner_state body report)
static std::atomic<float> _gravity[3];
static float _boot_gravity[3] = {};
static std::atomic<bool> _has_boot_gravity{false};

static void _imu_task(void* param)
{
    auto motion_detector = std::make_unique<MotionDetector>();
    motion_detector->setShakeThreshold(16.0f);

    while (1) {
        if (_bmi270 && _bmi270->update()) {
            auto& data = _bmi270->getData();
            // mclog::debug("IMU Accel: {:.2f}\t{:.2f}\t{:.2f}", data.accel_x, data.accel_y, data.accel_z);

            motion_detector->update(data.accel_x, data.accel_y, data.accel_z);

            static int samples = 0;
            float raw[3]       = {data.accel_x, data.accel_y, data.accel_z};
            for (int i = 0; i < 3; i++) {
                float g = samples == 0 ? raw[i] : _gravity[i].load() * 0.9f + raw[i] * 0.1f;
                _gravity[i].store(g);
            }
            if (++samples == 30 && !_has_boot_gravity.load()) {  // 3 s after boot, settled
                for (int i = 0; i < 3; i++) {
                    _boot_gravity[i] = _gravity[i].load();
                }
                _has_boot_gravity.store(true);
            }

            if (motion_detector->isShakeDetected()) {
                mclog::tagInfo(_tag, "Shake Detected!");
                GetHAL().onImuMotionEvent.emit(ImuMotionEvent::Shake);
            }
            // if (motion_detector->isPickUpDetected()) {
            //     mclog::tagInfo(_tag, "Pick Up Detected!");
            //     GetHAL().onImuMotionEvent.emit(ImuMotionEvent::PickUp);
            // }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Hal::imu_init()
{
    mclog::tagInfo(_tag, "init");

    auto i2c_bus = hal_bridge::board_get_i2c_bus();

    _bmi270 = std::make_unique<BMI270>(i2c_bus, 0x69);
    if (!_bmi270->begin()) {
        _bmi270.reset();
        mclog::tagError(_tag, "BMI270 init failed");
        return;
    }
    mclog::tagInfo(_tag, "BMI270 init ok");

    // xTaskCreateWithCaps(_imu_task, "imu", 4096, NULL, 2, NULL, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(_imu_task, "imu", 4096, NULL, 2, NULL, 1, MALLOC_CAP_SPIRAM);
}

int Hal::getTiltFromBootDegrees()
{
    if (!_has_boot_gravity.load()) {
        return -1;
    }
    float dot = 0, now2 = 0, boot2 = 0;
    for (int i = 0; i < 3; i++) {
        float g = _gravity[i].load();
        dot += g * _boot_gravity[i];
        now2 += g * g;
        boot2 += _boot_gravity[i] * _boot_gravity[i];
    }
    if (now2 <= 0 || boot2 <= 0) {
        return -1;
    }
    float c = std::max(-1.0f, std::min(1.0f, dot / std::sqrt(now2 * boot2)));
    return (int)std::lround(std::acos(c) * 180.0f / 3.14159265f);
}
