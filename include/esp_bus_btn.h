/**
 * @file esp_bus_btn.h
 * @brief ESP Bus - Button Module
 * 
 * @section Usage
 * 
 * @code{.c}
 * #include "esp_bus.h"
 * #include "esp_bus_btn.h"
 * 
 * void app_main(void) {
 *     esp_bus_init();
 *     
 *     // Register button on GPIO0 (active low, with internal pull-up)
 *     esp_bus_btn_reg("btn1", &(esp_bus_btn_cfg_t){
 *         .pin = GPIO_NUM_0,
 *         .active_low = true,
 *         .long_press_ms = 1000,    // Optional, default 1000
 *         .double_press_ms = 300,   // Optional, default 300
 *     });
 *     
 *     // Direct control
 *     uint8_t state;
 *     esp_bus_req(BTN_STATE("btn1"), NULL, 0, &state, sizeof(state), NULL, 100);
 *     
 *     // Subscribe to events
 *     esp_bus_sub(BTN_ON_SHORT("btn1"), on_short_press, NULL);
 *     esp_bus_sub("btn*:*", on_any_btn_event, NULL);  // Wildcard
 *     
 *     // Connect to LED (zero code)
 *     esp_bus_on(BTN_ON_SHORT("btn1"), LED_CMD_TOGGLE("led1"), NULL, 0);
 *     esp_bus_on(BTN_ON_LONG("btn1"), LED_CMD_BLINK("led1"), "100,100,3", 10);
 * }
 * 
 * void on_short_press(const char *evt, const void *data, size_t len, void *ctx) {
 *     printf("Short press!\n");
 * }
 * @endcode
 * 
 * @section Events
 * | Event | Data | Description |
 * |-------|------|-------------|
 * | short_press | - | Immediately on button press |
 * | long_press | - | While held >= long_press_ms |
 * | short_release | - | Released before long_press |
 * | long_release | - | Released after long_press |
 * | double_press | - | Double press detected |
 * 
 * @section Actions
 * | Action | Request | Response | Description |
 * |--------|---------|----------|-------------|
 * | get_state | - | uint8_t or btn_state_t | Get current state |
 * | config | btn_cfg_t | - | Reconfigure button |
 */

#pragma once

#include "esp_bus.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants (Compile-time safety)
// ============================================================================

// Actions
#define BTN_GET_STATE    "get_state"
#define BTN_WAIT_PRESS   "wait_press"
#define BTN_WAIT_RELEASE "wait_release"
#define BTN_CONFIG       "config"

// Events
#define BTN_SHORT        "short_press"
#define BTN_LONG         "long_press"
#define BTN_SHORT_REL    "short_release"
#define BTN_LONG_REL     "long_release"
#define BTN_DOUBLE       "double_press"

// Pattern builders (use module name as parameter)
#define BTN_STATE(name)      name "." BTN_GET_STATE
#define BTN_WAIT(name)       name "." BTN_WAIT_PRESS
#define BTN_CFG(name)        name "." BTN_CONFIG

#define BTN_ON_SHORT(name)      name ":" BTN_SHORT
#define BTN_ON_LONG(name)       name ":" BTN_LONG
#define BTN_ON_SHORT_REL(name)  name ":" BTN_SHORT_REL
#define BTN_ON_LONG_REL(name)   name ":" BTN_LONG_REL
#define BTN_ON_DOUBLE(name)     name ":" BTN_DOUBLE

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Button configuration
 */
typedef struct {
    gpio_num_t pin;             ///< GPIO pin
    bool active_low;            ///< True if pressed = LOW (default: true)
    uint32_t long_press_ms;     ///< Long press threshold (default: 1000)
    uint32_t double_press_ms;   ///< Double press window (default: 300)
    uint32_t debounce_ms;       ///< Debounce time (default: 20)
} esp_bus_btn_cfg_t;

/**
 * @brief Button state (for get_state response)
 */
typedef struct {
    uint8_t pressed;            ///< Current state (0/1)
    uint32_t press_count;       ///< Total press count
    int64_t last_press_ms;      ///< Last press timestamp
} esp_bus_btn_state_t;

// ============================================================================
// API
// ============================================================================

/**
 * @brief Register button module
 * @param name Module name (e.g., "btn1")
 * @param cfg Configuration
 * @return ESP_OK on success
 */
esp_err_t esp_bus_btn_reg(const char *name, const esp_bus_btn_cfg_t *cfg);

/**
 * @brief Unregister button module
 * @param name Module name
 * @return ESP_OK on success
 */
esp_err_t esp_bus_btn_unreg(const char *name);

// ============================================================================
// Schema (for validation)
// ============================================================================

extern const esp_bus_action_t esp_bus_btn_actions[];
extern const size_t esp_bus_btn_action_cnt;

extern const esp_bus_event_t esp_bus_btn_events[];
extern const size_t esp_bus_btn_event_cnt;

#ifdef __cplusplus
}
#endif
