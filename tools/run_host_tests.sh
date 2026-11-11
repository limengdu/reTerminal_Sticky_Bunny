#!/usr/bin/env bash

set -euo pipefail

# Compile every hardware-independent regression test with strict warnings.
# 使用严格警告选项编译全部不依赖硬件的回归测试。
readonly PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${TMPDIR:-/tmp}/sticky_bunny_host_tests"
readonly CXX_BIN="${CXX:-g++}"
readonly COMMON_FLAGS=(-std=c++17 -Wall -Wextra -Werror)

mkdir -p "${BUILD_DIR}"
cd "${PROJECT_ROOT}"

python3 tools/check_firmware_version.py
passed_count=1

compile_and_run() {
    local test_name="$1"
    shift

    echo "[host-test] ${test_name}"
    "${CXX_BIN}" "${COMMON_FLAGS[@]}" "$@" \
        -o "${BUILD_DIR}/${test_name}"
    "${BUILD_DIR}/${test_name}"
    passed_count=$((passed_count + 1))
}

compile_and_run battery_status_overlay_theme_test \
    -Isrc/ui \
    test/battery_status_overlay_theme_test.cpp \
    src/ui/battery_status_overlay_theme.cpp \
    src/ui/canvas.cpp src/ui/font.cpp

compile_and_run book_of_answers_state_test \
    -Isrc/apps/book_of_answers \
    test/book_of_answers_state_test.cpp \
    src/apps/book_of_answers/book_of_answers_state.cpp \
    src/apps/book_of_answers/book_of_answers_answers.cpp

compile_and_run book_of_answers_render_test \
    -Isrc/apps/book_of_answers -Isrc/ui -Isrc/ui/assets \
    test/book_of_answers_render_test.cpp \
    src/apps/book_of_answers/book_of_answers_pages.cpp \
    src/apps/book_of_answers/book_of_answers_state.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/book_of_answers_assets.cpp

compile_and_run desktop_pet_core_test \
    -Isrc/apps/desktop_pet/core \
    test/desktop_pet_core_test.cpp \
    src/apps/desktop_pet/core/pet_core.cpp \
    src/apps/desktop_pet/core/pet_dialogue.cpp \
    src/apps/desktop_pet/core/pet_idle_scheduler.cpp \
    src/apps/desktop_pet/core/pet_animation_queue.cpp \
    src/apps/desktop_pet/core/pet_save_record.cpp \
    src/apps/desktop_pet/core/pet_rtc_time.cpp

compile_and_run desktop_pet_daily_test \
    -Isrc/apps/desktop_pet \
    test/desktop_pet_daily_test.cpp \
    src/apps/desktop_pet/desktop_pet_daily.cpp

compile_and_run desktop_pet_outing_test \
    -Isrc/apps/desktop_pet \
    test/desktop_pet_outing_test.cpp \
    src/apps/desktop_pet/desktop_pet_outing.cpp

compile_and_run desktop_pet_power_policy_test \
    -Isrc/apps/desktop_pet -Isrc/apps/desktop_pet/core \
    test/desktop_pet_power_policy_test.cpp \
    src/apps/desktop_pet/desktop_pet_power_policy.cpp \
    src/apps/desktop_pet/desktop_pet_outing.cpp

compile_and_run desktop_pet_state_test \
    -DSTICKY_DESKTOP_PET_TEST_MODE=1 \
    -Isrc/apps/desktop_pet -Isrc/apps/desktop_pet/core \
    test/desktop_pet_state_test.cpp \
    src/apps/desktop_pet/desktop_pet_state.cpp \
    src/apps/desktop_pet/desktop_pet_daily.cpp \
    src/apps/desktop_pet/core/pet_core.cpp \
    src/apps/desktop_pet/core/pet_dialogue.cpp

compile_and_run desktop_pet_storage_record_test \
    -Isrc/apps/desktop_pet -Isrc/apps/desktop_pet/core \
    test/desktop_pet_storage_record_test.cpp \
    src/apps/desktop_pet/desktop_pet_storage_record.cpp \
    src/apps/desktop_pet/core/pet_save_record.cpp

compile_and_run desktop_pet_sound_cues_test \
    -DSTICKY_DESKTOP_PET_TEST_MODE=1 \
    -Isrc/apps/desktop_pet -Isrc/apps/desktop_pet/core -Isrc/devices \
    test/desktop_pet_sound_cues_test.cpp \
    src/apps/desktop_pet/desktop_pet_sound_cues.cpp \
    src/apps/desktop_pet/desktop_pet_state.cpp \
    src/apps/desktop_pet/desktop_pet_daily.cpp \
    src/apps/desktop_pet/core/pet_core.cpp \
    src/apps/desktop_pet/core/pet_dialogue.cpp

compile_and_run desktop_pet_render_test \
    -DSTICKY_DESKTOP_PET_TEST_MODE=1 \
    -Isrc/apps/desktop_pet -Isrc/apps/desktop_pet/core \
    -Isrc/ui -Isrc/ui/assets \
    test/desktop_pet_render_test.cpp \
    src/apps/desktop_pet/desktop_pet_pages.cpp \
    src/apps/desktop_pet/desktop_pet_state.cpp \
    src/apps/desktop_pet/desktop_pet_daily.cpp \
    src/apps/desktop_pet/core/pet_core.cpp \
    src/apps/desktop_pet/core/pet_dialogue.cpp \
    src/apps/desktop_pet/core/pet_rtc_time.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/desktop_pet_assets.cpp

compile_and_run onboarding_state_test \
    -Isrc/apps/onboarding \
    test/onboarding_state_test.cpp \
    src/apps/onboarding/onboarding_state.cpp

compile_and_run onboarding_pages_test \
    -O0 -Isrc/apps/onboarding -Isrc/ui -Isrc/ui/assets \
    test/onboarding_pages_test.cpp \
    src/apps/onboarding/onboarding_pages.cpp \
    src/apps/onboarding/onboarding_state.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/onboarding_assets.cpp

compile_and_run pixel_asset_test \
    -Isrc/ui -Isrc/ui/assets \
    test/pixel_asset_test.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/status_bunny_assets.cpp

compile_and_run pomodoro_countdown_policy_test \
    -Isrc/apps/pomodoro \
    test/pomodoro_countdown_policy_test.cpp

compile_and_run pomodoro_custom_input_test \
    -Isrc/apps/pomodoro \
    test/pomodoro_custom_input_test.cpp

compile_and_run pomodoro_render_policy_test \
    -Isrc/apps/pomodoro \
    test/pomodoro_render_policy_test.cpp

compile_and_run pomodoro_pages_test \
    -Isrc/apps/pomodoro -Isrc/ui -Isrc/ui/assets \
    test/pomodoro_pages_test.cpp \
    src/apps/pomodoro/pomodoro_pages.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/pomodoro_assets.cpp

compile_and_run status_board_state_test \
    -Isrc/apps/status_board -Isrc/ui -Isrc/ui/assets \
    test/status_board_state_test.cpp \
    src/apps/status_board/status_board_state.cpp \
    src/apps/status_board/status_board_pages.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/status_bunny_assets.cpp \
    src/ui/assets/pet_animation_assets.cpp

compile_and_run status_pet_animation_test \
    -Isrc/apps/status_board -Isrc/ui -Isrc/ui/assets \
    test/status_pet_animation_test.cpp \
    src/apps/status_board/status_pet_animation.cpp \
    src/ui/assets/pet_animation_assets.cpp

compile_and_run status_board_pages_test \
    -Isrc/apps/status_board -Isrc/ui -Isrc/ui/assets \
    test/status_board_pages_test.cpp \
    src/apps/status_board/status_board_pages.cpp \
    src/apps/status_board/status_board_state.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/status_bunny_assets.cpp \
    src/ui/assets/pet_animation_assets.cpp

compile_and_run status_board_render_test \
    -Isrc/apps/status_board -Isrc/ui -Isrc/ui/assets \
    test/status_board_render_test.cpp \
    src/apps/status_board/status_board_pages.cpp \
    src/apps/status_board/status_board_state.cpp \
    src/apps/status_board/status_pet_animation.cpp \
    src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/status_bunny_assets.cpp \
    src/ui/assets/pet_animation_assets.cpp

compile_and_run sticky_app_display_orientation_test \
    -Isrc/app -Isrc/sensors -Isrc/ui \
    test/sticky_app_display_orientation_test.cpp \
    src/app/sticky_app_display_orientation.cpp

compile_and_run sticky_app_gesture_test \
    -Isrc/app \
    test/sticky_app_gesture_test.cpp \
    src/app/sticky_app_gesture.cpp

compile_and_run sticky_app_launcher_render_test \
    -Isrc/app -Isrc/ui -Isrc/ui/assets \
    test/sticky_app_launcher_render_test.cpp \
    src/ui/app_pages.cpp src/ui/canvas.cpp src/ui/font.cpp \
    src/ui/assets/pixel_asset.cpp \
    src/ui/assets/app_launcher_assets.cpp \
    src/app/sticky_app_id.cpp

compile_and_run sticky_app_power_policy_test \
    -Isrc/app \
    test/sticky_app_power_policy_test.cpp \
    src/app/sticky_app_power_policy.cpp

compile_and_run sticky_app_router_test \
    -Isrc/app -Isrc/sensors \
    test/sticky_app_router_test.cpp \
    src/app/sticky_app_router.cpp

compile_and_run sticky_battery_protocol_test \
    -Isrc/devices \
    test/sticky_battery_protocol_test.cpp

compile_and_run sticky_touch_recovery_policy_test \
    -Isrc/input \
    test/sticky_touch_recovery_policy_test.cpp \
    src/input/sticky_touch_recovery_policy.cpp

compile_and_run sticky_shake_detector_test \
    -Isrc/sensors \
    test/sticky_shake_detector_test.cpp \
    src/sensors/sticky_shake_detector.cpp

compile_and_run sticky_display_spi_config_test \
    -Itest/epaper_stubs -Isrc/board \
    test/sticky_display_spi_config_test.cpp

echo "[host-test] PASS ${passed_count}/32"

# Compile the real panel drivers with simulated GPIO/SPI and controller responses.
# 使用模拟 GPIO/SPI 编译并验证真实屏幕驱动。
./tools/run_epaper_tests.sh
