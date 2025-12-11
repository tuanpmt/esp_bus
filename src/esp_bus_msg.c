/**
 * @file esp_bus_msg.c
 * @brief ESP Bus - Request, Event, Routing
 */

#include "esp_bus_priv.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "esp_bus";

// ============================================================================
// Request Processing
// ============================================================================

esp_err_t esp_bus_process_request(const char *pattern, const void *req, size_t req_len,
                                   void *res, size_t res_size, size_t *res_len) {
    char module_name[ESP_BUS_NAME_MAX];
    char action[ESP_BUS_NAME_MAX];
    char sep;
    
    if (!esp_bus_parse_pattern(pattern, module_name, action, &sep) || sep != '.') {
        esp_bus_report_error(pattern, ESP_ERR_INVALID_ARG, "invalid pattern");
        return ESP_ERR_INVALID_ARG;
    }
    
    module_node_t *mod = esp_bus_find_module(module_name);
    if (!mod) {
        if (g_bus.strict) {
            esp_bus_report_error(pattern, ESP_ERR_NOT_FOUND, "module not found");
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_OK;
    }
    
    if (!mod->on_req) {
        esp_bus_report_error(pattern, ESP_ERR_NOT_SUPPORTED, "no handler");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGD(TAG, "REQ %s", pattern);
    return mod->on_req(action, req, req_len, res, res_size, res_len, mod->ctx);
}

// ============================================================================
// Event Processing
// ============================================================================

void esp_bus_dispatch_event(const char *src, const char *evt, const void *data, size_t len) {
    char full[ESP_BUS_PATTERN_MAX];
    snprintf(full, sizeof(full), "%s:%s", src, evt);
    
    ESP_LOGD(TAG, "EVT %s", full);
    
    // Dispatch to subscribers
    sub_node_t *sub;
    SLIST_FOREACH(sub, &g_bus.subs, next) {
        if (esp_bus_match_pattern(sub->pattern, full)) {
            sub->handler(evt, data, len, sub->ctx);
        }
    }
    
    // Process routes
    route_node_t *r;
    SLIST_FOREACH(r, &g_bus.routes, next) {
        if (!esp_bus_match_pattern(r->evt_pattern, full)) continue;
        
        if (r->transform) {
            const char *out_req = NULL;
            void *out_data = NULL;
            size_t out_len = 0;
            r->transform(evt, data, len, &out_req, &out_data, &out_len, r->ctx);
            if (out_req) {
                ESP_LOGD(TAG, "ROUTE %s -> %s", full, out_req);
                esp_bus_process_request(out_req, out_data, out_len, NULL, 0, NULL);
            }
        } else {
            ESP_LOGD(TAG, "ROUTE %s -> %s", full, r->req_pattern);
            esp_bus_process_request(r->req_pattern, r->req_data, r->req_len, NULL, 0, NULL);
        }
    }
}

// ============================================================================
// Public API - Request
// ============================================================================

esp_err_t esp_bus_req(const char *pattern, const void *req, size_t req_len,
                       void *res, size_t res_size, size_t *res_len,
                       uint32_t timeout_ms) {
    if (!g_bus.initialized || !pattern) return ESP_ERR_INVALID_ARG;
    
    // If called from bus_task context (e.g. from service callback), 
    // process directly to avoid deadlock
    if (xTaskGetCurrentTaskHandle() == g_bus.task) {
        return esp_bus_process_request(pattern, req, req_len, res, res_size, res_len);
    }
    
    message_t msg = {
        .type = MSG_REQ,
        .res_buf = res,
        .res_size = res_size,
        .res_len = res_len,
    };
    
    strncpy(msg.pattern, pattern, ESP_BUS_PATTERN_MAX - 1);
    
    if (req && req_len > 0) {
        msg.data = malloc(req_len);
        if (!msg.data) return ESP_ERR_NO_MEM;
        memcpy(msg.data, req, req_len);
        msg.len = req_len;
    }
    
    esp_err_t result = ESP_OK;
    SemaphoreHandle_t done = NULL;
    
    if (timeout_ms > 0) {
        done = xSemaphoreCreateBinary();
        msg.done = done;
        msg.result = &result;
    }
    
    if (xQueueSend(g_bus.queue, &msg, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        if (msg.data) free(msg.data);
        if (done) vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }
    
    if (done) {
        if (xSemaphoreTake(done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
        }
        vSemaphoreDelete(done);
    }
    
    return result;
}

// ============================================================================
// Public API - Events
// ============================================================================

esp_err_t esp_bus_emit(const char *src, const char *evt, const void *data, size_t len) {
    if (!g_bus.initialized || !src || !evt) return ESP_ERR_INVALID_ARG;
    
    message_t msg = { .type = MSG_EVT };
    snprintf(msg.pattern, ESP_BUS_PATTERN_MAX, "%s:%s", src, evt);
    
    if (data && len > 0) {
        msg.data = malloc(len);
        if (!msg.data) return ESP_ERR_NO_MEM;
        memcpy(msg.data, data, len);
        msg.len = len;
    }
    
    if (xQueueSend(g_bus.queue, &msg, 0) != pdTRUE) {
        if (msg.data) free(msg.data);
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

int esp_bus_sub(const char *pattern, esp_bus_evt_fn handler, void *ctx) {
    if (!g_bus.initialized || !pattern || !handler) return -1;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    sub_node_t *node = calloc(1, sizeof(sub_node_t));
    if (!node) {
        xSemaphoreGive(g_bus.mutex);
        return -1;
    }
    
    node->id = g_bus.next_sub_id++;
    strncpy(node->pattern, pattern, ESP_BUS_PATTERN_MAX - 1);
    node->handler = handler;
    node->ctx = ctx;
    
    SLIST_INSERT_HEAD(&g_bus.subs, node, next);
    xSemaphoreGive(g_bus.mutex);
    
    ESP_LOGD(TAG, "Sub '%s' id=%d", pattern, node->id);
    return node->id;
}

void esp_bus_unsub(int id) {
    if (!g_bus.initialized || id < 0) return;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    sub_node_t *node;
    SLIST_FOREACH(node, &g_bus.subs, next) {
        if (node->id == id) {
            SLIST_REMOVE(&g_bus.subs, node, sub_node, next);
            free(node);
            break;
        }
    }
    
    xSemaphoreGive(g_bus.mutex);
}

// ============================================================================
// Public API - Routing
// ============================================================================

esp_err_t esp_bus_on(const char *evt_pattern, const char *req_pattern,
                      const void *req_data, size_t req_len) {
    if (!g_bus.initialized || !evt_pattern || !req_pattern) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    route_node_t *node = calloc(1, sizeof(route_node_t));
    if (!node) {
        xSemaphoreGive(g_bus.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(node->evt_pattern, evt_pattern, ESP_BUS_PATTERN_MAX - 1);
    strncpy(node->req_pattern, req_pattern, ESP_BUS_PATTERN_MAX - 1);
    
    if (req_data && req_len > 0) {
        node->req_data = malloc(req_len);
        if (node->req_data) {
            memcpy(node->req_data, req_data, req_len);
            node->req_len = req_len;
        }
    }
    
    SLIST_INSERT_HEAD(&g_bus.routes, node, next);
    xSemaphoreGive(g_bus.mutex);
    
    ESP_LOGD(TAG, "Route '%s' -> '%s'", evt_pattern, req_pattern);
    return ESP_OK;
}

esp_err_t esp_bus_on_fn(const char *evt_pattern, esp_bus_transform_fn fn, void *ctx) {
    if (!g_bus.initialized || !evt_pattern || !fn) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    route_node_t *node = calloc(1, sizeof(route_node_t));
    if (!node) {
        xSemaphoreGive(g_bus.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(node->evt_pattern, evt_pattern, ESP_BUS_PATTERN_MAX - 1);
    node->transform = fn;
    node->ctx = ctx;
    
    SLIST_INSERT_HEAD(&g_bus.routes, node, next);
    xSemaphoreGive(g_bus.mutex);
    
    return ESP_OK;
}

esp_err_t esp_bus_off(const char *evt_pattern, const char *req_pattern) {
    if (!g_bus.initialized || !evt_pattern) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    route_node_t *r, *tmp;
    SLIST_FOREACH_SAFE(r, &g_bus.routes, next, tmp) {
        if (strcmp(r->evt_pattern, evt_pattern) == 0) {
            if (!req_pattern || strcmp(r->req_pattern, req_pattern) == 0) {
                SLIST_REMOVE(&g_bus.routes, r, route_node, next);
                if (r->req_data) free(r->req_data);
                free(r);
            }
        }
    }
    
    xSemaphoreGive(g_bus.mutex);
    return ESP_OK;
}

