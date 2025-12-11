/**
 * @file esp_bus_led.h
 * @brief ESP Bus - LED Module
 * 
 * @section Usage
 * 
 * @code{.c}
 * #include "esp_bus.h"
 * #include "esp_bus_led.h"
 * 
 * void app_main(void) {
 *     esp_bus_init();
 *     
 *     // Register LED on GPIO2
 *     esp_bus_led_reg("led1", &(esp_bus_led_cfg_t){
 *         .pin = GPIO_NUM_2,
 *         .active_low = false,  // LED on = HIGH
 *     });
 *     
 *     // Basic control
 *     esp_bus_call(LED_CMD_ON("led1"));
 *     esp_bus_call(LED_CMD_OFF("led1"));
 *     esp_bus_call(LED_CMD_TOGGLE("led1"));
 *     
 *     // Blink: on_ms, off_ms, count (-1 = infinite)
 *     esp_bus_call_s(LED_CMD_BLINK("led1"), "100,100,5");   // Blink 5 times
 *     esp_bus_call_s(LED_CMD_BLINK("led1"), "500,500,-1");  // Blink forever
 *     esp_bus_call(LED_CMD_BLINK("led1"));                  // Default 200ms
 *     
 *     // Get state
 *     uint8_t state;
 *     esp_bus_req(LED_CMD_STATE("led1"), NULL, 0, &state, sizeof(state), NULL, 100);
 *     
 *     // Connect button to LED
 *     esp_bus_on(BTN_ON_SHORT("btn1"), LED_CMD_BLINK("led1"), "100,100,3", 10);
 *     esp_bus_on(BTN_ON_LONG("btn1"), LED_CMD_ON("led1"), NULL, 0);
 *     esp_bus_on(BTN_ON_SHORT_REL("btn1"), LED_CMD_OFF("led1"), NULL, 0);
 * }
 * @endcode
 * 
 * @section Actions
 * | Action | Request | Response | Description |
 * |--------|---------|----------|-------------|
 * | on | - | - | Turn LED on |
 * | off | - | - | Turn LED off |
 * | toggle | - | - | Toggle LED state |
 * | blink | string "on,off[,count]" | - | Blink LED |
 * | get_state | - | uint8_t | Get LED state (0/1) |
 * 
 * @section Blink Format
 * - `"on_ms,off_ms"` - Blink forever
 * - `"on_ms,off_ms,count"` - Blink count times
 * - `"on_ms,off_ms,-1"` - Blink forever (explicit)
 * - `"on_ms,off_ms,0"` - Stop blinking
 * - Default (no params): 200ms on, 200ms off, forever
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
#define LED_ON           "on"
#define LED_OFF          "off"
#define LED_TOGGLE       "toggle"
#define LED_BLINK        "blink"
#define LED_PATTERN      "pattern"
#define LED_GET_STATE    "get_state"

// Pattern builders (use module name as parameter)
#define LED_CMD_ON(name)      name "." LED_ON
#define LED_CMD_OFF(name)     name "." LED_OFF
#define LED_CMD_TOGGLE(name)  name "." LED_TOGGLE
#define LED_CMD_BLINK(name)   name "." LED_BLINK
#define LED_CMD_STATE(name)   name "." LED_GET_STATE

// ============================================================================
// Types
// ============================================================================

/**
 * @brief LED configuration
 */
typedef struct {
    gpio_num_t pin;             ///< GPIO pin
    bool active_low;            ///< True if LED on = LOW (default: false)
} esp_bus_led_cfg_t;

// ============================================================================
// API
// ============================================================================

/**
 * @brief Register LED module
 * @param name Module name (e.g., "led1")
 * @param cfg Configuration
 * @return ESP_OK on success
 */
esp_err_t esp_bus_led_reg(const char *name, const esp_bus_led_cfg_t *cfg);

/**
 * @brief Unregister LED module
 * @param name Module name
 * @return ESP_OK on success
 */
esp_err_t esp_bus_led_unreg(const char *name);

// ============================================================================
// Schema (for validation)
// ============================================================================

extern const esp_bus_action_t esp_bus_led_actions[];
extern const size_t esp_bus_led_action_cnt;

#ifdef __cplusplus
}
#endif
