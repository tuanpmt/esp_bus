/**
 * @file esp_bus_btn.c
 * @brief ESP Bus - Button Module Implementation
 */

#include "esp_bus_btn.h"
#include "esp_bus_priv.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "esp_bus_btn";

// ============================================================================
// Schema
// ============================================================================

const esp_bus_action_t esp_bus_btn_actions[] = {
    {BTN_GET_STATE,    "none", "btn_state_t", "Get button state"},
    {BTN_WAIT_PRESS,   "none", "none",        "Block until pressed"},
    {BTN_WAIT_RELEASE, "none", "none",        "Block until released"},
    {BTN_CONFIG,       "btn_cfg_t", "none",   "Reconfigure button"},
};
const size_t esp_bus_btn_action_cnt = sizeof(esp_bus_btn_actions) / sizeof(esp_bus_btn_actions[0]);

const esp_bus_event_t esp_bus_btn_events[] = {
    {BTN_SHORT,     "none",  "Short press (immediately on press)"},
    {BTN_LONG,      "none",  "Long press (while held >= long_press_ms)"},
    {BTN_SHORT_REL, "none",  "Short release (released before long_press)"},
    {BTN_LONG_REL,  "none",  "Long release (released after long_press)"},
    {BTN_DOUBLE,    "none",  "Double press detected"},
};
const size_t esp_bus_btn_event_cnt = sizeof(esp_bus_btn_events) / sizeof(esp_bus_btn_events[0]);

// ============================================================================
// Context
// ============================================================================

typedef struct {
    char name[ESP_BUS_NAME_MAX];
    gpio_num_t pin;
    bool active_low;
    uint32_t long_press_ms;
    uint32_t double_press_ms;
    uint32_t debounce_ms;
    
    // State
    uint8_t state;              // Current logical state (1=pressed)
    uint8_t raw_state;          // Raw GPIO state
    uint8_t last_raw_state;     // Previous raw state
    uint32_t press_count;
    int64_t press_time_ms;      // When button was pressed
    int64_t release_time_ms;    // When button was released
    int64_t last_press_ms;      // Last press for double detection
    int64_t debounce_until_ms;  // Debounce end time
    bool long_fired;            // Long press event already fired
    
    int tick_id;                // Service tick ID
} btn_ctx_t;

// ============================================================================
// Helpers
// ============================================================================

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static uint8_t read_pin(btn_ctx_t *btn) {
    uint8_t raw = gpio_get_level(btn->pin);
    return btn->active_low ? !raw : raw;
}

// ============================================================================
// Tick Handler
// ============================================================================

static void btn_tick(void *ctx) {
    btn_ctx_t *btn = (btn_ctx_t *)ctx;
    int64_t now = now_ms();
    
    // Debounce
    if (now < btn->debounce_until_ms) {
        return;
    }
    
    uint8_t current = read_pin(btn);
    
    if (current != btn->raw_state) {
        btn->raw_state = current;
        btn->debounce_until_ms = now + btn->debounce_ms;
        return; // Wait for debounce
    }
    
    if (current != btn->state) {
        btn->state = current;
        
        if (current) {
            // Button pressed
            btn->press_time_ms = now;
            btn->long_fired = false;
            btn->press_count++;
            
            // Emit short_press immediately
            esp_bus_emit(btn->name, BTN_SHORT, NULL, 0);
            
            // Check double press
            if (btn->last_press_ms > 0 && 
                (now - btn->last_press_ms) < btn->double_press_ms) {
                esp_bus_emit(btn->name, BTN_DOUBLE, NULL, 0);
            }
            btn->last_press_ms = now;
            
        } else {
            // Button released
            // Ignore release if no press was recorded (startup condition)
            if (btn->press_time_ms == 0) {
                return;
            }
            
            btn->release_time_ms = now;
            btn->press_time_ms = 0; // Reset for next press
            
            // Emit short_release or long_release
            if (btn->long_fired) {
                esp_bus_emit(btn->name, BTN_LONG_REL, NULL, 0);
            } else {
                esp_bus_emit(btn->name, BTN_SHORT_REL, NULL, 0);
            }
        }
    }
    
    // Check long press while held
    if (btn->state && !btn->long_fired) {
        int64_t held = now - btn->press_time_ms;
        if (held >= btn->long_press_ms) {
            btn->long_fired = true;
            esp_bus_emit(btn->name, BTN_LONG, NULL, 0);
        }
    }
}

// ============================================================================
// Request Handler
// ============================================================================

static esp_err_t btn_req_handler(const char *action, const void *req, size_t req_len,
                                  void *res, size_t res_size, size_t *res_len, void *ctx) {
    btn_ctx_t *btn = (btn_ctx_t *)ctx;
    
    if (strcmp(action, BTN_GET_STATE) == 0) {
        if (res && res_size >= sizeof(esp_bus_btn_state_t)) {
            esp_bus_btn_state_t *state = (esp_bus_btn_state_t *)res;
            state->pressed = btn->state;
            state->press_count = btn->press_count;
            state->last_press_ms = btn->last_press_ms;
            if (res_len) *res_len = sizeof(esp_bus_btn_state_t);
        } else if (res && res_size >= sizeof(uint8_t)) {
            *(uint8_t *)res = btn->state;
            if (res_len) *res_len = sizeof(uint8_t);
        }
        return ESP_OK;
    }
    
    if (strcmp(action, BTN_CONFIG) == 0) {
        if (req && req_len >= sizeof(esp_bus_btn_cfg_t)) {
            const esp_bus_btn_cfg_t *cfg = (const esp_bus_btn_cfg_t *)req;
            if (cfg->long_press_ms > 0) btn->long_press_ms = cfg->long_press_ms;
            if (cfg->double_press_ms > 0) btn->double_press_ms = cfg->double_press_ms;
            if (cfg->debounce_ms > 0) btn->debounce_ms = cfg->debounce_ms;
        }
        return ESP_OK;
    }
    
    // wait_press and wait_release would need blocking implementation
    // For now, return not supported
    
    return ESP_ERR_NOT_SUPPORTED;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t esp_bus_btn_reg(const char *name, const esp_bus_btn_cfg_t *cfg) {
    if (!name || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate context
    btn_ctx_t *ctx = calloc(1, sizeof(btn_ctx_t));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(ctx->name, name, ESP_BUS_NAME_MAX - 1);
    ctx->pin = cfg->pin;
    ctx->active_low = cfg->active_low;
    ctx->long_press_ms = cfg->long_press_ms > 0 ? cfg->long_press_ms : 1000;
    ctx->double_press_ms = cfg->double_press_ms > 0 ? cfg->double_press_ms : 300;
    ctx->debounce_ms = cfg->debounce_ms > 0 ? cfg->debounce_ms : 20;
    
    // Configure GPIO
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << cfg->pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = cfg->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = cfg->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_cfg);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    
    // Read initial state
    ctx->raw_state = read_pin(ctx);
    ctx->state = ctx->raw_state;
    ctx->last_raw_state = ctx->raw_state;
    
    // Register module
    esp_bus_module_t mod = {
        .name = name,
        .on_req = btn_req_handler,
        .ctx = ctx,
        .actions = esp_bus_btn_actions,
        .action_cnt = esp_bus_btn_action_cnt,
        .events = esp_bus_btn_events,
        .event_cnt = esp_bus_btn_event_cnt,
    };
    
    err = esp_bus_reg(&mod);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    
    // Register tick (poll every 10ms)
    ctx->tick_id = esp_bus_tick(btn_tick, 10, ctx);
    if (ctx->tick_id < 0) {
        esp_bus_unreg(name);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Registered '%s' on GPIO%d", name, cfg->pin);
    return ESP_OK;
}

esp_err_t esp_bus_btn_unreg(const char *name) {
    if (!name) return ESP_ERR_INVALID_ARG;
    module_node_t *mod = esp_bus_acquire_module(name);
    if (!mod) return ESP_ERR_NOT_FOUND;
    btn_ctx_t *ctx = (btn_ctx_t *)mod->ctx;
    int tick_id = ctx ? ctx->tick_id : -1;
    esp_bus_release_module(mod);
    
    if (tick_id >= 0) {
        esp_bus_tick_del(tick_id);
    }
    
    esp_err_t err = esp_bus_unreg(name);
    if (ctx) {
        free(ctx);
    }
    return err;
}

