/**
 * @file esp_bus_msg.c
 * @brief ESP Bus - Request, Event, Routing
 */

#include "esp_bus_priv.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "esp_bus";

// ============================================================================
// Local retain/release for subs and routes
// ============================================================================
static sub_node_t *retain_sub_locked(sub_node_t *node) {
    if (!node || node->pending_delete) return NULL;
    node->refcnt++;
    return node;
}

static void release_sub(sub_node_t *node) {
    if (!node) return;
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    if (node->refcnt > 0) node->refcnt--;
    if (node->pending_delete && node->refcnt == 0) {
        sub_node_t *prev = NULL;
        sub_node_t *cur;
        SLIST_FOREACH(cur, &g_bus.subs, next) {
            if (cur == node) break;
            prev = cur;
        }
        if (cur) {
            if (prev) {
                SLIST_REMOVE_AFTER(prev, next);
            } else {
                SLIST_REMOVE_HEAD(&g_bus.subs, next);
            }
            free(cur);
        }
    }
    xSemaphoreGive(g_bus.mutex);
}

static route_node_t *retain_route_locked(route_node_t *node) {
    if (!node || node->pending_delete) return NULL;
    node->refcnt++;
    return node;
}

static void release_route(route_node_t *node) {
    if (!node) return;
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    if (node->refcnt > 0) node->refcnt--;
    if (node->pending_delete && node->refcnt == 0) {
        route_node_t *prev = NULL;
        route_node_t *cur;
        SLIST_FOREACH(cur, &g_bus.routes, next) {
            if (cur == node) break;
            prev = cur;
        }
        if (cur) {
            if (prev) {
                SLIST_REMOVE_AFTER(prev, next);
            } else {
                SLIST_REMOVE_HEAD(&g_bus.routes, next);
            }
            if (cur->req_data) free(cur->req_data);
            free(cur);
        }
    }
    xSemaphoreGive(g_bus.mutex);
}

// ============================================================================
// Request Processing
// ============================================================================

esp_err_t esp_bus_process_request(const char *pattern, const void *req, size_t req_len,
                                   void *res, size_t res_size, size_t *res_len) {
    char module_name[ESP_BUS_NAME_MAX];
    char action[ESP_BUS_PATTERN_MAX];
    char sep;
    
    if (!esp_bus_parse_pattern(pattern, module_name, action, &sep) || sep != '.') {
        esp_bus_report_error(pattern, ESP_ERR_INVALID_ARG, "invalid pattern");
        return ESP_ERR_INVALID_ARG;
    }
    
    module_node_t *mod = esp_bus_acquire_module(module_name);
    if (!mod) {
        if (g_bus.strict) {
            esp_bus_report_error(pattern, ESP_ERR_NOT_FOUND, "module not found");
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_OK;
    }
    
    if (!mod->on_req) {
        esp_bus_release_module(mod);
        esp_bus_report_error(pattern, ESP_ERR_NOT_SUPPORTED, "no handler");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGD(TAG, "REQ %s", pattern);
    esp_err_t ret = mod->on_req(action, req, req_len, res, res_size, res_len, mod->ctx);
    esp_bus_release_module(mod);
    return ret;
}

// ============================================================================
// Event Processing
// ============================================================================

void esp_bus_dispatch_event(const char *src, const char *evt, const void *data, size_t len) {
    char full[ESP_BUS_PATTERN_MAX];
    snprintf(full, sizeof(full), "%s:%s", src, evt);
    
    ESP_LOGD(TAG, "EVT %s", full);
    
    // Collect subscribers and routes safely
    sub_node_t **subs = NULL;
    size_t subs_cnt = 0, subs_cap = 0;
    route_node_t **routes = NULL;
    size_t routes_cnt = 0, routes_cap = 0;
    bool oom = false;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    sub_node_t *sub;
    SLIST_FOREACH(sub, &g_bus.subs, next) {
        if (sub->pending_delete) continue;
        if (esp_bus_match_pattern(sub->pattern, full)) {
            if (subs_cnt == subs_cap) {
                subs_cap = subs_cap ? subs_cap * 2 : 4;
                void *tmp_mem = realloc(subs, subs_cap * sizeof(*subs));
                if (!tmp_mem) { oom = true; break; }
                subs = tmp_mem;
            }
            subs[subs_cnt++] = retain_sub_locked(sub);
        }
    }
    
    route_node_t *r;
    if (!oom) {
        SLIST_FOREACH(r, &g_bus.routes, next) {
            if (r->pending_delete) continue;
            if (!esp_bus_match_pattern(r->evt_pattern, full)) continue;
            if (routes_cnt == routes_cap) {
                routes_cap = routes_cap ? routes_cap * 2 : 4;
                void *tmp_mem = realloc(routes, routes_cap * sizeof(*routes));
                if (!tmp_mem) { oom = true; break; }
                routes = tmp_mem;
            }
            routes[routes_cnt++] = retain_route_locked(r);
        }
    }
    xSemaphoreGive(g_bus.mutex);
    if (oom) {
        free(subs);
        free(routes);
        ESP_LOGE(TAG, "EVT %s: OOM during dispatch", full);
        return;
    }
    
    // Dispatch to subscribers
    for (size_t i = 0; i < subs_cnt; i++) {
        if (subs[i]) {
            subs[i]->handler(evt, data, len, subs[i]->ctx);
            release_sub(subs[i]);
        }
    }
    
    // Process routes
    for (size_t i = 0; i < routes_cnt; i++) {
        r = routes[i];
        if (!r) continue;
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
        release_route(r);
    }
    
    free(subs);
    free(routes);
}

// ============================================================================
// Public API - Request
// ============================================================================

esp_err_t esp_bus_req(const char *pattern, const void *req, size_t req_len,
                       void *res, size_t res_size, size_t *res_len,
                       uint32_t timeout_ms) {
    if (!g_bus.initialized || !pattern) return ESP_ERR_INVALID_ARG;
    if (timeout_ms == ESP_BUS_NO_WAIT && res) return ESP_ERR_INVALID_ARG;
    
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
    
    TickType_t start = xTaskGetTickCount();
    TickType_t queue_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0) {
        done = xSemaphoreCreateBinary();
        msg.done = done;
        msg.result = &result;
    }
    
    if (xQueueSend(g_bus.queue, &msg, queue_ticks) != pdTRUE) {
        if (msg.data) free(msg.data);
        if (done) vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }
    
    if (done) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t remain = (queue_ticks > elapsed) ? (queue_ticks - elapsed) : 0;
        if (xSemaphoreTake(done, remain) != pdTRUE) {
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
    node->refcnt = 0;
    node->pending_delete = false;
    
    SLIST_INSERT_HEAD(&g_bus.subs, node, next);
    xSemaphoreGive(g_bus.mutex);
    
    ESP_LOGD(TAG, "Sub '%s' id=%d", pattern, node->id);
    return node->id;
}

void esp_bus_unsub(int id) {
    if (!g_bus.initialized || id < 0) return;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    sub_node_t *node;
    sub_node_t *prev = NULL;
    SLIST_FOREACH(node, &g_bus.subs, next) {
        if (node->id == id) {
            if (node->refcnt == 0) {
                if (prev) {
                    SLIST_REMOVE_AFTER(prev, next);
                } else {
                    SLIST_REMOVE_HEAD(&g_bus.subs, next);
                }
                free(node);
            } else {
                node->pending_delete = true;
            }
            break;
        }
        prev = node;
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
    node->refcnt = 0;
    node->pending_delete = false;
    
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
    node->refcnt = 0;
    node->pending_delete = false;
    
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
                if (r->refcnt == 0) {
                    SLIST_REMOVE(&g_bus.routes, r, route_node, next);
                    if (r->req_data) free(r->req_data);
                    free(r);
                } else {
                    r->pending_delete = true;
                }
            }
        }
    }
    
    xSemaphoreGive(g_bus.mutex);
    return ESP_OK;
}

