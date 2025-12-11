/**
 * @file esp_bus.h
 * @brief ESP Bus - Event-Driven Message Bus for ESP-IDF
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/queue.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define ESP_BUS_NO_WAIT       0
#define ESP_BUS_WAIT_MAX      portMAX_DELAY

#ifdef CONFIG_ESP_BUS_NAME_MAX_LEN
#define ESP_BUS_NAME_MAX      CONFIG_ESP_BUS_NAME_MAX_LEN
#else
#define ESP_BUS_NAME_MAX      16
#endif

#ifdef CONFIG_ESP_BUS_PATTERN_MAX_LEN
#define ESP_BUS_PATTERN_MAX   CONFIG_ESP_BUS_PATTERN_MAX_LEN
#else
#define ESP_BUS_PATTERN_MAX   32
#endif

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Request handler callback
 */
typedef esp_err_t (*esp_bus_req_fn)(
    const char *action,
    const void *req, size_t req_len,
    void *res, size_t res_size, size_t *res_len,
    void *ctx
);

/**
 * @brief Event handler callback
 */
typedef void (*esp_bus_evt_fn)(
    const char *event,
    const void *data, size_t len,
    void *ctx
);

/**
 * @brief Service callback (tick/timer)
 */
typedef void (*esp_bus_svc_fn)(void *ctx);

/**
 * @brief Transform callback for routing
 */
typedef void (*esp_bus_transform_fn)(
    const char *evt, const void *data, size_t len,
    const char **out_req, void **out_data, size_t *out_len,
    void *ctx
);

/**
 * @brief Error callback
 */
typedef void (*esp_bus_err_fn)(const char *pattern, esp_err_t err, const char *msg);

/**
 * @brief Action schema
 */
typedef struct {
    const char *name;
    const char *req_type;
    const char *res_type;
    const char *desc;
} esp_bus_action_t;

/**
 * @brief Event schema
 */
typedef struct {
    const char *name;
    const char *data_type;
    const char *desc;
} esp_bus_event_t;

/**
 * @brief Module configuration
 */
typedef struct {
    const char *name;
    esp_bus_req_fn on_req;
    esp_bus_evt_fn on_evt;
    void *ctx;
    
    // Schema (optional)
    const esp_bus_action_t *actions;
    size_t action_cnt;
    const esp_bus_event_t *events;
    size_t event_cnt;
} esp_bus_module_t;

// ============================================================================
// Core API
// ============================================================================

/**
 * @brief Check if esp_bus is initialized
 * @return true if initialized
 */
bool esp_bus_is_init(void);

/**
 * @brief Initialize esp_bus
 * 
 * Safe to call multiple times - returns ESP_OK if already initialized.
 * 
 * @return ESP_OK on success (including already initialized)
 */
esp_err_t esp_bus_init(void);

/**
 * @brief Deinitialize esp_bus
 * @return ESP_OK on success
 */
esp_err_t esp_bus_deinit(void);

/**
 * @brief Register a module
 * @param module Module configuration
 * @return ESP_OK on success
 */
esp_err_t esp_bus_reg(const esp_bus_module_t *module);

/**
 * @brief Unregister a module
 * @param name Module name
 * @return ESP_OK on success
 */
esp_err_t esp_bus_unreg(const char *name);

// ============================================================================
// Request API
// ============================================================================

/**
 * @brief Send request to module
 * @param pattern Pattern "module.action"
 * @param req Request data
 * @param req_len Request length
 * @param res Response buffer (user-provided)
 * @param res_size Response buffer size
 * @param res_len Actual response length (output)
 * @param timeout_ms Timeout in ms
 * @return ESP_OK on success
 */
esp_err_t esp_bus_req(
    const char *pattern,
    const void *req, size_t req_len,
    void *res, size_t res_size, size_t *res_len,
    uint32_t timeout_ms
);

/**
 * @brief Call without response
 */
#define esp_bus_call(p) \
    esp_bus_req(p, NULL, 0, NULL, 0, NULL, ESP_BUS_NO_WAIT)

/**
 * @brief Call with string data
 */
#define esp_bus_call_s(p, s) \
    esp_bus_req(p, s, strlen(s)+1, NULL, 0, NULL, ESP_BUS_NO_WAIT)

// ============================================================================
// Event API
// ============================================================================

/**
 * @brief Emit event
 * @param src Source module name
 * @param evt Event name
 * @param data Event data
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t esp_bus_emit(const char *src, const char *evt, const void *data, size_t len);

/**
 * @brief Subscribe to events
 * @param pattern Pattern "module:event" (supports wildcards)
 * @param handler Event handler
 * @param ctx User context
 * @return Subscription ID (>=0) or negative error
 */
int esp_bus_sub(const char *pattern, esp_bus_evt_fn handler, void *ctx);

/**
 * @brief Unsubscribe
 * @param id Subscription ID
 */
void esp_bus_unsub(int id);

// ============================================================================
// Routing API
// ============================================================================

/**
 * @brief Connect event to request
 * @param evt_pattern Event pattern
 * @param req_pattern Request pattern
 * @param req_data Request data to send
 * @param req_len Request data length
 * @return ESP_OK on success
 */
esp_err_t esp_bus_on(
    const char *evt_pattern,
    const char *req_pattern,
    const void *req_data, size_t req_len
);

/**
 * @brief Connect with transform
 */
esp_err_t esp_bus_on_fn(const char *evt_pattern, esp_bus_transform_fn fn, void *ctx);

/**
 * @brief Disconnect route
 */
esp_err_t esp_bus_off(const char *evt_pattern, const char *req_pattern);

// ============================================================================
// Service API (Shared Task)
// ============================================================================

/**
 * @brief Add tick service (called at interval)
 * @param fn Callback function
 * @param interval_ms Interval in ms
 * @param ctx User context
 * @return Service ID (>=0) or negative error
 */
int esp_bus_tick(esp_bus_svc_fn fn, uint32_t interval_ms, void *ctx);

/**
 * @brief Remove tick service
 */
void esp_bus_tick_del(int id);

/**
 * @brief One-shot timer
 * @param fn Callback function
 * @param delay_ms Delay in ms
 * @param ctx User context
 * @return Timer ID (>=0) or negative error
 */
int esp_bus_after(esp_bus_svc_fn fn, uint32_t delay_ms, void *ctx);

/**
 * @brief Repeating timer
 * @param fn Callback function
 * @param interval_ms Interval in ms
 * @param ctx User context
 * @return Timer ID (>=0) or negative error
 */
int esp_bus_every(esp_bus_svc_fn fn, uint32_t interval_ms, void *ctx);

/**
 * @brief Cancel timer
 */
void esp_bus_cancel(int id);

/**
 * @brief Trigger task wake up (from task)
 */
void esp_bus_trigger(void);

/**
 * @brief Trigger task wake up (from ISR)
 */
void esp_bus_trigger_isr(BaseType_t *woken);

// ============================================================================
// Query API
// ============================================================================

bool esp_bus_exists(const char *module);
bool esp_bus_has_action(const char *module, const char *action);
bool esp_bus_has_event(const char *module, const char *event);

// ============================================================================
// Config API
// ============================================================================

esp_err_t esp_bus_log_level(esp_log_level_t level);
esp_err_t esp_bus_strict(bool enable);
esp_err_t esp_bus_on_err(esp_bus_err_fn cb);

#ifdef __cplusplus
}
#endif
