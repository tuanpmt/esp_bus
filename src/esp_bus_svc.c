/**
 * @file esp_bus_svc.c
 * @brief ESP Bus - Service Loop (tick, timer)
 */

#include "esp_bus_priv.h"
#include <stdlib.h>

static void release_service(svc_node_t *node);

// ============================================================================
// Service Processing
// ============================================================================

uint32_t esp_bus_calc_next_wait(void) {
    int64_t now = esp_bus_now_us();
    int64_t min_wait = 100000; // 100ms max
    
    svc_node_t *s;
    SLIST_FOREACH(s, &g_bus.services, next) {
        if (s->pending_delete || s->next_run_us <= 0) continue;
        if (s->next_run_us > 0) {
            int64_t wait = s->next_run_us - now;
            if (wait < 1000) wait = 1000; // Min 1ms to avoid WDT
            if (wait < min_wait) min_wait = wait;
        }
    }
    
    uint32_t wait_ms = (uint32_t)(min_wait / 1000);
    return wait_ms > 0 ? wait_ms : 1; // Ensure at least 1ms
}

void esp_bus_run_services(void) {
    int64_t now = esp_bus_now_us();
    
    svc_node_t **run_list = NULL;
    size_t run_cnt = 0, run_cap = 0;
    bool oom = false;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    svc_node_t *s;
    SLIST_FOREACH(s, &g_bus.services, next) {
        if (s->pending_delete || s->next_run_us <= 0) continue;
        if (now >= s->next_run_us) {
            if (run_cnt == run_cap) {
                run_cap = run_cap ? run_cap * 2 : 4;
                void *tmp_mem = realloc(run_list, run_cap * sizeof(*run_list));
                if (!tmp_mem) { oom = true; break; }
                run_list = tmp_mem;
            }
            s->refcnt++;
            run_list[run_cnt++] = s;
            
            if (s->repeat) {
                s->next_run_us = now + (int64_t)s->interval_ms * 1000;
            } else {
                s->next_run_us = 0;
            }
        }
    }
    
    // Clean up nodes that are done and not referenced
    svc_node_t *tmp;
    SLIST_FOREACH_SAFE(s, &g_bus.services, next, tmp) {
        if ((s->pending_delete && s->refcnt == 0) ||
            (!s->repeat && s->next_run_us == 0 && s->refcnt == 0)) {
            SLIST_REMOVE(&g_bus.services, s, svc_node, next);
            free(s);
        }
    }
    xSemaphoreGive(g_bus.mutex);
    if (oom) {
        free(run_list);
        return;
    }
    
    for (size_t i = 0; i < run_cnt; i++) {
        svc_node_t *node = run_list[i];
        if (!node) continue;
        node->fn(node->ctx);
        release_service(node);
    }
    
    free(run_list);
}

// ============================================================================
// Internal Helper
// ============================================================================

static int add_service(esp_bus_svc_fn fn, uint32_t interval_ms, void *ctx, bool repeat) {
    if (!g_bus.initialized || !fn) return -1;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    svc_node_t *node = calloc(1, sizeof(svc_node_t));
    if (!node) {
        xSemaphoreGive(g_bus.mutex);
        return -1;
    }
    
    node->id = g_bus.next_svc_id++;
    node->fn = fn;
    node->ctx = ctx;
    node->interval_ms = interval_ms;
    node->next_run_us = esp_bus_now_us() + (int64_t)interval_ms * 1000;
    node->repeat = repeat;
    node->refcnt = 0;
    node->pending_delete = false;
    
    SLIST_INSERT_HEAD(&g_bus.services, node, next);
    xSemaphoreGive(g_bus.mutex);
    
    esp_bus_trigger();
    return node->id;
}

static void remove_service(int id) {
    if (!g_bus.initialized || id < 0) return;
    
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    
    svc_node_t *node;
    svc_node_t *prev = NULL;
    SLIST_FOREACH(node, &g_bus.services, next) {
        if (node->id == id) {
            if (node->refcnt == 0) {
                if (prev) {
                    SLIST_REMOVE_AFTER(prev, next);
                } else {
                    SLIST_REMOVE_HEAD(&g_bus.services, next);
                }
                free(node);
            } else {
                node->pending_delete = true;
                node->next_run_us = 0;
            }
            break;
        }
        prev = node;
    }
    
    xSemaphoreGive(g_bus.mutex);
}

// ============================================================================
// Public API - Services
// ============================================================================

int esp_bus_tick(esp_bus_svc_fn fn, uint32_t interval_ms, void *ctx) {
    return add_service(fn, interval_ms, ctx, true);
}

void esp_bus_tick_del(int id) {
    remove_service(id);
}

int esp_bus_after(esp_bus_svc_fn fn, uint32_t delay_ms, void *ctx) {
    return add_service(fn, delay_ms, ctx, false);
}

int esp_bus_every(esp_bus_svc_fn fn, uint32_t interval_ms, void *ctx) {
    return add_service(fn, interval_ms, ctx, true);
}

void esp_bus_cancel(int id) {
    remove_service(id);
}

// ============================================================================
// Refcount release
// ============================================================================

static void release_service(svc_node_t *node) {
    if (!node) return;
    xSemaphoreTake(g_bus.mutex, portMAX_DELAY);
    if (node->refcnt > 0) node->refcnt--;
    if ((node->pending_delete || (!node->repeat && node->next_run_us == 0)) && node->refcnt == 0) {
        svc_node_t *prev = NULL;
        svc_node_t *cur;
        SLIST_FOREACH(cur, &g_bus.services, next) {
            if (cur == node) break;
            prev = cur;
        }
        if (cur) {
            if (prev) {
                SLIST_REMOVE_AFTER(prev, next);
            } else {
                SLIST_REMOVE_HEAD(&g_bus.services, next);
            }
            free(cur);
        }
    }
    xSemaphoreGive(g_bus.mutex);
}

void esp_bus_trigger(void) {
    if (!g_bus.initialized) return;
    message_t msg = { .type = MSG_TRIGGER };
    xQueueSend(g_bus.queue, &msg, 0);
}

void esp_bus_trigger_isr(BaseType_t *woken) {
    if (!g_bus.initialized) return;
    message_t msg = { .type = MSG_TRIGGER };
    xQueueSendFromISR(g_bus.queue, &msg, woken);
}

