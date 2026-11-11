cmake_minimum_required(VERSION 3.16)
project(unifi_preview C)
get_filename_component(REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPOSITORY}/managed_components/lvgl__lvgl" lvgl)
add_executable(unifi_preview render.c
    "${REPOSITORY}/main/unifi_lvgl.c"
    "${REPOSITORY}/main/unifi_view.c"
    "${REPOSITORY}/main/unifi_model.c"
    "${REPOSITORY}/main/unifi_demo.c"
    "${REPOSITORY}/assets/fonts/unifi_font_12.c"
    "${REPOSITORY}/assets/fonts/unifi_font_14.c"
    "${REPOSITORY}/assets/fonts/unifi_font_18.c"
    "${REPOSITORY}/assets/fonts/unifi_font_24.c")
target_include_directories(unifi_preview PRIVATE
    "${REPOSITORY}/main" "${REPOSITORY}/tests" "${REPOSITORY}/assets/fonts")
target_compile_options(unifi_preview PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(unifi_preview PRIVATE lvgl m)
