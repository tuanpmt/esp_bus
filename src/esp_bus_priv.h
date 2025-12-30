/**
 * @file esp_bus_priv.h
 * @brief ESP Bus - Private/Internal definitions
 */

#pragma once

#include "esp_bus.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <sys/queue.h>

// ============================================================================
// Internal Types
// ============================================================================

typedef struct module_node {
    char name[ESP_BUS_NAME_MAX];
    esp_bus_req_fn on_req;
    esp_bus_evt_fn on_evt;
    void *ctx;
    uint16_t refcnt;
    bool pending_delete;
    const esp_bus_action_t *actions;
    size_t action_cnt;
    const esp_bus_event_t *events;
    size_t event_cnt;
    SLIST_ENTRY(module_node) next;
} module_node_t;

typedef struct sub_node {
    int id;
    char pattern[ESP_BUS_PATTERN_MAX];
    esp_bus_evt_fn handler;
    void *ctx;
    uint16_t refcnt;
    bool pending_delete;
    SLIST_ENTRY(sub_node) next;
} sub_node_t;

typedef struct route_node {
    char evt_pattern[ESP_BUS_PATTERN_MAX];
    char req_pattern[ESP_BUS_PATTERN_MAX];
    void *req_data;
    size_t req_len;
    esp_bus_transform_fn transform;
    void *ctx;
    uint16_t refcnt;
    bool pending_delete;
    SLIST_ENTRY(route_node) next;
} route_node_t;

typedef struct svc_node {
    int id;
    esp_bus_svc_fn fn;
    void *ctx;
    uint32_t interval_ms;
    int64_t next_run_us;
    bool repeat;
    uint16_t refcnt;
    bool pending_delete;
    SLIST_ENTRY(svc_node) next;
} svc_node_t;

typedef enum {
    MSG_REQ,
    MSG_EVT,
    MSG_TRIGGER,
} msg_type_t;

typedef struct {
    msg_type_t type;
    char pattern[ESP_BUS_PATTERN_MAX];
    void *data;
    size_t len;
    void *res_buf;
    size_t res_size;
    size_t *res_len;
    esp_err_t *result;
    SemaphoreHandle_t done;
} message_t;

// List Heads
SLIST_HEAD(module_list, module_node);
SLIST_HEAD(sub_list, sub_node);
SLIST_HEAD(route_list, route_node);
SLIST_HEAD(svc_list, svc_node);

// ============================================================================
// Global State
// ============================================================================

typedef struct {
    bool initialized;
    bool strict;
    esp_log_level_t log_level;
    esp_bus_err_fn on_err;
    
    struct module_list modules;
    struct sub_list subs;
    struct route_list routes;
    struct svc_list services;
    
    int next_sub_id;
    int next_svc_id;
    
    QueueHandle_t queue;
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
} esp_bus_state_t;

extern esp_bus_state_t g_bus;

// ============================================================================
// Internal Functions
// ============================================================================

// Helpers
int64_t esp_bus_now_us(void);
bool esp_bus_match_pattern(const char *pattern, const char *target);
bool esp_bus_parse_pattern(const char *pattern, char *module, char *action, char *sep);
module_node_t *esp_bus_acquire_module(const char *name);
void esp_bus_release_module(module_node_t *mod);
void esp_bus_report_error(const char *pattern, esp_err_t err, const char *msg);

// Processing
esp_err_t esp_bus_process_request(const char *pattern, const void *req, size_t req_len,
                                   void *res, size_t res_size, size_t *res_len);
void esp_bus_dispatch_event(const char *src, const char *evt, const void *data, size_t len);

// Services
uint32_t esp_bus_calc_next_wait(void);
void esp_bus_run_services(void);

