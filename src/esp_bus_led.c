/**
 * @file esp_bus_led.c
 * @brief ESP Bus - LED Module Implementation
 */

#include "esp_bus_led.h"
#include "esp_bus_priv.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "esp_bus_led";

// ============================================================================
// Schema
// ============================================================================

const esp_bus_action_t esp_bus_led_actions[] = {
    {LED_ON,        "none",   "none",  "Turn LED on"},
    {LED_OFF,       "none",   "none",  "Turn LED off"},
    {LED_TOGGLE,    "none",   "none",  "Toggle LED state"},
    {LED_BLINK,     "string", "none",  "Blink LED: 'on_ms,off_ms[,count]'"},
    {LED_PATTERN,   "string", "none",  "LED pattern: 't1,t2,t3,...'"},
    {LED_GET_STATE, "none",   "uint8", "Get LED state (0/1)"},
};
const size_t esp_bus_led_action_cnt = sizeof(esp_bus_led_actions) / sizeof(esp_bus_led_actions[0]);

// ============================================================================
// Context
// ============================================================================

typedef struct {
    char name[ESP_BUS_NAME_MAX];
    gpio_num_t pin;
    bool active_low;
    
    // State
    uint8_t state;              // Current state (0/1)
    
    // Blink
    uint16_t on_ms;
    uint16_t off_ms;
    int16_t blink_count;        // -1=infinite, 0=stopped
    int timer_id;               // Timer ID for blink
} led_ctx_t;

// ============================================================================
// Helpers
// ============================================================================

static void led_set_hw(led_ctx_t *led, uint8_t state) {
    gpio_set_level(led->pin, led->active_low ? !state : state);
}

static void led_set(led_ctx_t *led, uint8_t state) {
    led->state = state;
    led_set_hw(led, state);
}

static void led_toggle(led_ctx_t *led) {
    led_set(led, !led->state);
}

static void led_stop_blink(led_ctx_t *led) {
    if (led->timer_id >= 0) {
        esp_bus_cancel(led->timer_id);
        led->timer_id = -1;
    }
    led->blink_count = 0;
}

// ============================================================================
// Blink Timer
// ============================================================================

static void led_blink_step(void *ctx) {
    led_ctx_t *led = (led_ctx_t *)ctx;
    
    // Toggle state
    led_toggle(led);
    
    // Check if done
    if (led->blink_count > 0) {
        led->blink_count--;
        if (led->blink_count == 0) {
            led_set(led, 0);  // Turn off when done
            led->timer_id = -1;
            return;
        }
    }
    
    // Schedule next toggle
    uint32_t next_ms = led->state ? led->on_ms : led->off_ms;
    led->timer_id = esp_bus_after(led_blink_step, next_ms, led);
}

static void led_start_blink(led_ctx_t *led, uint16_t on_ms, uint16_t off_ms, int16_t count) {
    led_stop_blink(led);
    
    if (count == 0) return;
    
    led->on_ms = on_ms > 0 ? on_ms : 200;
    led->off_ms = off_ms > 0 ? off_ms : 200;
    led->blink_count = count < 0 ? -1 : count * 2;  // count * 2 for on/off cycles
    
    led_set(led, 1);  // Start with LED on
    led->timer_id = esp_bus_after(led_blink_step, led->on_ms, led);
}

// ============================================================================
// Parse Helpers
// ============================================================================

static bool parse_blink_params(const char *str, uint16_t *on_ms, uint16_t *off_ms, int16_t *count) {
    *on_ms = 200;
    *off_ms = 200;
    *count = -1;
    
    if (!str || !*str) return true;
    
    int parsed = sscanf(str, "%hu,%hu,%hd", on_ms, off_ms, count);
    return parsed >= 1;
}

// ============================================================================
// Request Handler
// ============================================================================

static esp_err_t led_req_handler(const char *action, const void *req, size_t req_len,
                                  void *res, size_t res_size, size_t *res_len, void *ctx) {
    led_ctx_t *led = (led_ctx_t *)ctx;
    
    if (strcmp(action, LED_ON) == 0) {
        led_stop_blink(led);
        led_set(led, 1);
        return ESP_OK;
    }
    
    if (strcmp(action, LED_OFF) == 0) {
        led_stop_blink(led);
        led_set(led, 0);
        return ESP_OK;
    }
    
    if (strcmp(action, LED_TOGGLE) == 0) {
        led_stop_blink(led);
        led_toggle(led);
        return ESP_OK;
    }
    
    if (strcmp(action, LED_BLINK) == 0) {
        uint16_t on_ms, off_ms;
        int16_t count;
        
        const char *params = (req && req_len > 0) ? (const char *)req : NULL;
        parse_blink_params(params, &on_ms, &off_ms, &count);
        
        led_start_blink(led, on_ms, off_ms, count);
        return ESP_OK;
    }
    
    if (strcmp(action, LED_GET_STATE) == 0) {
        if (res && res_size >= sizeof(uint8_t)) {
            *(uint8_t *)res = led->state;
            if (res_len) *res_len = sizeof(uint8_t);
        }
        return ESP_OK;
    }
    
    if (strcmp(action, LED_PATTERN) == 0) {
        // Pattern implementation could be added here
        // Format: "t1,t2,t3,..." alternating on/off times
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_ERR_NOT_SUPPORTED;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t esp_bus_led_reg(const char *name, const esp_bus_led_cfg_t *cfg) {
    if (!name || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate context
    led_ctx_t *ctx = calloc(1, sizeof(led_ctx_t));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(ctx->name, name, ESP_BUS_NAME_MAX - 1);
    ctx->pin = cfg->pin;
    ctx->active_low = cfg->active_low;
    ctx->timer_id = -1;
    
    // Configure GPIO
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << cfg->pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_cfg);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    
    // Set initial state (off)
    led_set(ctx, 0);
    
    // Register module
    esp_bus_module_t mod = {
        .name = name,
        .on_req = led_req_handler,
        .ctx = ctx,
        .actions = esp_bus_led_actions,
        .action_cnt = esp_bus_led_action_cnt,
    };
    
    err = esp_bus_reg(&mod);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    
    ESP_LOGI(TAG, "Registered '%s' on GPIO%d", name, cfg->pin);
    return ESP_OK;
}

esp_err_t esp_bus_led_unreg(const char *name) {
    if (!name) return ESP_ERR_INVALID_ARG;
    module_node_t *mod = esp_bus_acquire_module(name);
    if (!mod) return ESP_ERR_NOT_FOUND;
    led_ctx_t *ctx = (led_ctx_t *)mod->ctx;
    int timer_id = ctx ? ctx->timer_id : -1;
    esp_bus_release_module(mod);
    
    if (timer_id >= 0) {
        esp_bus_cancel(timer_id);
    }
    
    esp_err_t err = esp_bus_unreg(name);
    if (ctx) {
        free(ctx);
    }
    return err;
}

