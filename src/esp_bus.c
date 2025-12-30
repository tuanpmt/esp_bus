/**
 * @file esp_bus.c
 * @brief ESP Bus - Core (init, task, module registration)
 */

#include "esp_bus_priv.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "esp_bus";

// Global state
esp_bus_state_t g_bus;

// ============================================================================
// Refcount helpers
// ============================================================================
static void remove_module_node(module_node_t *target) {
    module_node_t *prev = NULL;
    module_node_t *node;
    SLIST_FOREACH(node, &g_bus.modules, next) {
        if (node == target) break;
        prev = node;
    }
    if (!node) return;
    if (prev) {
        SLIST_REMOVE_AFTER(prev, next);
    } else {
        SLIST_REMOVE_HEAD(&g_bus.modules, next);
    }
    free(node);
}

module_node_t *esp_bus_acquire_module(const char *name) {
    if (!g_bus.initialized || !name) return NULL;
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    module_node_t *node;
    SLIST_FOREACH(node, &g_bus.modules, next) {
        if (node->pending_delete) continue;
        if (strcmp(node->name, name) == 0) {
            node->refcnt++;
            xSemaphoreGive(g_bus.mutex);
            return node;
        }
    }
    xSemaphoreGive(g_bus.mutex);
    return NULL;
}

void esp_bus_release_module(module_node_t *mod) {
    if (!mod) return;
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    if (mod->refcnt > 0) mod->refcnt--;
    if (mod->pending_delete && mod->refcnt == 0) {
        remove_module_node(mod);
        xSemaphoreGive(g_bus.mutex);
        return;
    }
    xSemaphoreGive(g_bus.mutex);
}

// ============================================================================
// Helpers
// ============================================================================

int64_t esp_bus_now_us(void) {
    return esp_timer_get_time();
}

bool esp_bus_match_pattern(const char *pattern, const char *target) {
    const char *p = pattern;
    const char *t = target;
    
    while (*p && *t) {
        if (*p == '?') {
            p++;
            t++;
            continue;
        }
        if (*p == '*') {
            p++;
            if (*p == '\0') return true;
            while (*t) {
                if (esp_bus_match_pattern(p, t)) return true;
                t++;
            }
            return false;
        }
        if (*p != *t) return false;
        p++;
        t++;
    }
    
    while (*p == '*') p++;
    return (*p == '\0' && *t == '\0');
}

bool esp_bus_parse_pattern(const char *pattern, char *module, char *action, char *sep) {
    const char *dot = strchr(pattern, '.');
    const char *colon = strchr(pattern, ':');
    
    if (dot) {
        size_t len = dot - pattern;
        if (len >= ESP_BUS_NAME_MAX) return false;
        strncpy(module, pattern, len);
        module[len] = '\0';
        strncpy(action, dot + 1, ESP_BUS_PATTERN_MAX - 1);
        action[ESP_BUS_PATTERN_MAX - 1] = '\0';
        *sep = '.';
        return true;
    }
    
    if (colon) {
        size_t len = colon - pattern;
        if (len >= ESP_BUS_NAME_MAX) return false;
        strncpy(module, pattern, len);
        module[len] = '\0';
        strncpy(action, colon + 1, ESP_BUS_PATTERN_MAX - 1);
        action[ESP_BUS_PATTERN_MAX - 1] = '\0';
        *sep = ':';
        return true;
    }
    
    strncpy(module, pattern, ESP_BUS_NAME_MAX - 1);
    module[ESP_BUS_NAME_MAX - 1] = '\0';
    action[0] = '\0';
    *sep = '\0';
    return true;
}

module_node_t *esp_bus_find_module(const char *name) {
    // Deprecated: kept for backward compatibility inside this file
    return esp_bus_acquire_module(name);
}

void esp_bus_report_error(const char *pattern, esp_err_t err, const char *msg) {
    if (g_bus.log_level <= ESP_LOG_WARN) {
        ESP_LOGW(TAG, "%s: %s (0x%x)", pattern, msg, err);
    }
    if (g_bus.on_err) {
        g_bus.on_err(pattern, err, msg);
    }
}

// ============================================================================
// Task
// ============================================================================

static void process_message(message_t *msg) {
    switch (msg->type) {
        case MSG_REQ: {
            esp_err_t err = esp_bus_process_request(msg->pattern, msg->data, msg->len,
                                                    msg->res_buf, msg->res_size, msg->res_len);
            if (msg->result) *msg->result = err;
            if (msg->done) xSemaphoreGive(msg->done);
            if (msg->data) free(msg->data);
            break;
        }
        case MSG_EVT: {
            char module[ESP_BUS_NAME_MAX], event[ESP_BUS_NAME_MAX];
            char sep;
            if (esp_bus_parse_pattern(msg->pattern, module, event, &sep) && sep == ':') {
                esp_bus_dispatch_event(module, event, msg->data, msg->len);
            }
            if (msg->data) free(msg->data);
            break;
        }
        case MSG_TRIGGER:
            break;
    }
}

static void bus_task(void *arg) {
    message_t msg;
    TickType_t last_service_tick = 0;
    
    ESP_LOGI(TAG, "Task started");
    
    while (1) {
        uint32_t wait_ms = esp_bus_calc_next_wait();
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        if (wait_ticks == 0) wait_ticks = 1; // Minimum 1 tick
        
        // Wait for message or timeout
        if (xQueueReceive(g_bus.queue, &msg, wait_ticks) == pdTRUE) {
            process_message(&msg);
            
            // Drain remaining messages (non-blocking)
            while (xQueueReceive(g_bus.queue, &msg, 0) == pdTRUE) {
                process_message(&msg);
            }
        }
        
        // Run services at most once per tick to prevent tight loop
        TickType_t now = xTaskGetTickCount();
        if (now != last_service_tick) {
            last_service_tick = now;
            esp_bus_run_services();
        }
    }
}

// ============================================================================
// Init / Deinit
// ============================================================================

bool esp_bus_is_init(void) {
    return g_bus.initialized;
}

esp_err_t esp_bus_init(void) {
    // Already initialized - return OK (shared service pattern)
    if (g_bus.initialized) {
        ESP_LOGD(TAG, "Already initialized");
        return ESP_OK;
    }
    
    memset(&g_bus, 0, sizeof(g_bus));
    g_bus.log_level = ESP_LOG_INFO;
    
    SLIST_INIT(&g_bus.modules);
    SLIST_INIT(&g_bus.subs);
    SLIST_INIT(&g_bus.routes);
    SLIST_INIT(&g_bus.services);
    
    #ifdef CONFIG_ESP_BUS_QUEUE_SIZE
    #define BUS_QUEUE_SIZE CONFIG_ESP_BUS_QUEUE_SIZE
    #else
    #define BUS_QUEUE_SIZE 16
    #endif
    
    g_bus.queue = xQueueCreate(BUS_QUEUE_SIZE, sizeof(message_t));
    if (!g_bus.queue) return ESP_ERR_NO_MEM;
    
    g_bus.mutex = xSemaphoreCreateMutex();
    if (!g_bus.mutex) {
        vQueueDelete(g_bus.queue);
        return ESP_ERR_NO_MEM;
    }
    
    #ifdef CONFIG_ESP_BUS_TASK_STACK_SIZE
    #define BUS_STACK_SIZE CONFIG_ESP_BUS_TASK_STACK_SIZE
    #else
    #define BUS_STACK_SIZE 4096
    #endif
    
    #ifdef CONFIG_ESP_BUS_TASK_PRIORITY
    #define BUS_PRIORITY CONFIG_ESP_BUS_TASK_PRIORITY
    #else
    #define BUS_PRIORITY 5
    #endif
    
    if (xTaskCreate(bus_task, "esp_bus", BUS_STACK_SIZE, NULL, BUS_PRIORITY, &g_bus.task) != pdPASS) {
        vSemaphoreDelete(g_bus.mutex);
        vQueueDelete(g_bus.queue);
        return ESP_ERR_NO_MEM;
    }
    
    g_bus.initialized = true;
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t esp_bus_deinit(void) {
    if (!g_bus.initialized) return ESP_ERR_INVALID_STATE;
    
    if (g_bus.task) {
        vTaskDelete(g_bus.task);
        g_bus.task = NULL;
    }
    
    // Free modules
    module_node_t *mod, *mod_tmp;
    SLIST_FOREACH_SAFE(mod, &g_bus.modules, next, mod_tmp) {
        SLIST_REMOVE(&g_bus.modules, mod, module_node, next);
        free(mod);
    }
    
    // Free subscriptions
    sub_node_t *sub, *sub_tmp;
    SLIST_FOREACH_SAFE(sub, &g_bus.subs, next, sub_tmp) {
        SLIST_REMOVE(&g_bus.subs, sub, sub_node, next);
        free(sub);
    }
    
    // Free routes
    route_node_t *r, *r_tmp;
    SLIST_FOREACH_SAFE(r, &g_bus.routes, next, r_tmp) {
        SLIST_REMOVE(&g_bus.routes, r, route_node, next);
        if (r->req_data) free(r->req_data);
        free(r);
    }
    
    // Free services
    svc_node_t *s, *s_tmp;
    SLIST_FOREACH_SAFE(s, &g_bus.services, next, s_tmp) {
        SLIST_REMOVE(&g_bus.services, s, svc_node, next);
        free(s);
    }
    
    if (g_bus.mutex) { vSemaphoreDelete(g_bus.mutex); g_bus.mutex = NULL; }
    if (g_bus.queue) { vQueueDelete(g_bus.queue); g_bus.queue = NULL; }
    
    g_bus.initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

// ============================================================================
// Module Registration
// ============================================================================

esp_err_t esp_bus_reg(const esp_bus_module_t *module) {
    if (!g_bus.initialized || !module || !module->name) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);

    // Check if module already exists (inline to avoid deadlock with esp_bus_acquire_module)
    module_node_t *existing = NULL;
    module_node_t *iter;
    SLIST_FOREACH(iter, &g_bus.modules, next) {
        if (!iter->pending_delete && strcmp(iter->name, module->name) == 0) {
            existing = iter;
            break;
        }
    }
    if (existing) {
        xSemaphoreGive(g_bus.mutex);
        ESP_LOGE(TAG, "Module '%s' already registered", module->name);
        return ESP_ERR_INVALID_STATE;
    }
    
    module_node_t *node = calloc(1, sizeof(module_node_t));
    if (!node) {
        xSemaphoreGive(g_bus.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(node->name, module->name, ESP_BUS_NAME_MAX - 1);
    node->on_req = module->on_req;
    node->on_evt = module->on_evt;
    node->ctx = module->ctx;
    node->actions = module->actions;
    node->action_cnt = module->action_cnt;
    node->events = module->events;
    node->event_cnt = module->event_cnt;
    
    SLIST_INSERT_HEAD(&g_bus.modules, node, next);
    xSemaphoreGive(g_bus.mutex);
    
    ESP_LOGI(TAG, "Registered '%s'", module->name);
    return ESP_OK;
}

esp_err_t esp_bus_unreg(const char *name) {
    if (!g_bus.initialized || !name) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    module_node_t *mod;
    module_node_t *prev = NULL;
    SLIST_FOREACH(mod, &g_bus.modules, next) {
        if (strcmp(mod->name, name) == 0 && !mod->pending_delete) break;
        prev = mod;
    }
    if (!mod) {
        xSemaphoreGive(g_bus.mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (mod->refcnt > 0) {
        mod->pending_delete = true;
        xSemaphoreGive(g_bus.mutex);
        return ESP_OK;
    }
    
    if (prev) {
        SLIST_REMOVE_AFTER(prev, next);
    } else {
        SLIST_REMOVE_HEAD(&g_bus.modules, next);
    }
    free(mod);
    xSemaphoreGive(g_bus.mutex);
    
    ESP_LOGI(TAG, "Unregistered '%s'", name);
    return ESP_OK;
}

// ============================================================================
// Query
// ============================================================================

bool esp_bus_exists(const char *module) {
    if (!g_bus.initialized || !module) return false;
    module_node_t *mod = esp_bus_acquire_module(module);
    bool exists = (mod != NULL);
    esp_bus_release_module(mod);
    return exists;
}

bool esp_bus_has_action(const char *module, const char *action) {
    if (!g_bus.initialized || !module || !action) return false;
    
    bool found = false;
    module_node_t *mod = esp_bus_acquire_module(module);
    if (mod && mod->actions) {
        for (size_t i = 0; i < mod->action_cnt; i++) {
            if (strcmp(mod->actions[i].name, action) == 0) {
                found = true;
                break;
            }
        }
    }
    esp_bus_release_module(mod);
    return found;
}

bool esp_bus_has_event(const char *module, const char *event) {
    if (!g_bus.initialized || !module || !event) return false;
    
    bool found = false;
    module_node_t *mod = esp_bus_acquire_module(module);
    if (mod && mod->events) {
        for (size_t i = 0; i < mod->event_cnt; i++) {
            if (strcmp(mod->events[i].name, event) == 0) {
                found = true;
                break;
            }
        }
    }
    esp_bus_release_module(mod);
    return found;
}

// ============================================================================
// Config
// ============================================================================

esp_err_t esp_bus_log_level(esp_log_level_t level) {
    g_bus.log_level = level;
    esp_log_level_set(TAG, level);
    return ESP_OK;
}

esp_err_t esp_bus_strict(bool enable) {
    g_bus.strict = enable;
    return ESP_OK;
}

esp_err_t esp_bus_on_err(esp_bus_err_fn cb) {
    g_bus.on_err = cb;
    return ESP_OK;
}

