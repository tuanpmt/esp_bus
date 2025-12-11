/**
 * @file test_esp_bus.c
 * @brief Unit tests for ESP Bus component
 */

#include "unity.h"
#include "esp_bus.h"
#include "esp_bus_btn.h"
#include "esp_bus_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>

// ============================================================================
// Memory Leak Detection Helpers
// ============================================================================

static size_t heap_before = 0;

#define MEMORY_CHECK_START() \
    do { \
        heap_caps_check_integrity_all(true); \
        heap_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT); \
    } while(0)

#define MEMORY_CHECK_END(tolerance) \
    do { \
        vTaskDelay(pdMS_TO_TICKS(100)); \
        heap_caps_check_integrity_all(true); \
        size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT); \
        int leaked = (int)heap_before - (int)heap_after; \
        if (leaked > (int)(tolerance)) { \
            printf("MEMORY LEAK: %d bytes (tolerance: %d)\n", leaked, (int)(tolerance)); \
            TEST_FAIL_MESSAGE("Memory leak detected"); \
        } \
    } while(0)

// ============================================================================
// Test Fixtures
// ============================================================================

static int test_counter = 0;
static char last_event[32] = {0};
static char last_action[32] = {0};

static void reset_test_state(void) {
    test_counter = 0;
    last_event[0] = '\0';
    last_action[0] = '\0';
}

// ============================================================================
// Test Module
// ============================================================================

static esp_err_t test_req_handler(const char *action,
                                   const void *req, size_t req_len,
                                   void *res, size_t res_size, size_t *res_len,
                                   void *ctx) {
    strncpy(last_action, action, sizeof(last_action) - 1);
    test_counter++;
    
    if (strcmp(action, "echo") == 0 && req && res) {
        size_t copy_len = req_len < res_size ? req_len : res_size;
        memcpy(res, req, copy_len);
        if (res_len) *res_len = copy_len;
        return ESP_OK;
    }
    
    if (strcmp(action, "get_counter") == 0 && res && res_size >= sizeof(int)) {
        *(int *)res = test_counter;
        if (res_len) *res_len = sizeof(int);
        return ESP_OK;
    }
    
    if (strcmp(action, "fail") == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

static void test_evt_handler(const char *event, const void *data, size_t len, void *ctx) {
    if (event) {
        strncpy(last_event, event, sizeof(last_event) - 1);
    }
    test_counter++;
}

// Service callback (different signature from event handler)
static void test_svc_handler(void *ctx) {
    test_counter++;
}

// ============================================================================
// Core Tests
// ============================================================================

TEST_CASE("esp_bus_init initializes correctly", "[esp_bus][core]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    TEST_ASSERT_TRUE(esp_bus_is_init());
    
    // Multiple init should be OK
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
    TEST_ASSERT_FALSE(esp_bus_is_init());
}

TEST_CASE("esp_bus_reg registers module", "[esp_bus][core]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "test1",
        .on_req = test_req_handler,
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    TEST_ASSERT_TRUE(esp_bus_exists("test1"));
    
    // Duplicate registration should fail
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, esp_bus_reg(&mod));
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("test1"));
    TEST_ASSERT_FALSE(esp_bus_exists("test1"));
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Request Tests
// ============================================================================

TEST_CASE("esp_bus_req sends request to module", "[esp_bus][request]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "test",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    // Simple call
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_call("test.action1"));
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL_STRING("action1", last_action);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("test"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("esp_bus_req echo test", "[esp_bus][request]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "test",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    // Echo request with response
    char req_data[] = "hello";
    char res_buf[32] = {0};
    size_t res_len = 0;
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_req("test.echo", req_data, sizeof(req_data),
                                          res_buf, sizeof(res_buf), &res_len, 100));
    TEST_ASSERT_EQUAL_STRING("hello", res_buf);
    TEST_ASSERT_EQUAL(sizeof(req_data), res_len);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("test"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("esp_bus_req strict mode", "[esp_bus][request]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Non-strict mode: unknown module returns OK (esp_bus_req is sync)
    esp_bus_strict(false);
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_req("unknown.action", NULL, 0, NULL, 0, NULL, 100));
    
    // Strict mode: unknown module returns NOT_FOUND
    esp_bus_strict(true);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, esp_bus_req("unknown.action", NULL, 0, NULL, 0, NULL, 100));
    
    esp_bus_strict(false);
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Event Tests
// ============================================================================

TEST_CASE("esp_bus_emit and subscribe", "[esp_bus][event]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Subscribe to events
    int sub_id = esp_bus_sub("src1:*", test_evt_handler, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sub_id);
    
    // Emit event
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_emit("src1", "test_event", NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(50));
    
    TEST_ASSERT_EQUAL_STRING("test_event", last_event);
    TEST_ASSERT_EQUAL(1, test_counter);
    
    // Unsubscribe
    esp_bus_unsub(sub_id);
    
    // Emit again - should not trigger handler
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_emit("src1", "another_event", NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(0, test_counter);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("esp_bus_sub wildcard matching", "[esp_bus][event]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Subscribe with wildcard
    int sub_id = esp_bus_sub("btn*:short_press", test_evt_handler, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sub_id);
    
    // Should match
    esp_bus_emit("btn1", "short_press", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, test_counter);
    
    esp_bus_emit("btn2", "short_press", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(2, test_counter);
    
    // Should NOT match
    esp_bus_emit("led1", "short_press", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(2, test_counter);
    
    esp_bus_emit("btn1", "long_press", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(2, test_counter);
    
    esp_bus_unsub(sub_id);
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Routing Tests
// ============================================================================

TEST_CASE("esp_bus_on routes event to request", "[esp_bus][routing]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "target",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    // Setup route
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_on("src:trigger", "target.action", NULL, 0));
    
    // Emit event - should trigger request
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_emit("src", "trigger", NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(50));
    
    TEST_ASSERT_EQUAL_STRING("action", last_action);
    TEST_ASSERT_EQUAL(1, test_counter);
    
    // Disconnect route
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_off("src:trigger", "target.action"));
    
    // Emit again - should NOT trigger
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_emit("src", "trigger", NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(0, test_counter);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("target"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Service Tests
// ============================================================================

TEST_CASE("esp_bus_tick periodic callback", "[esp_bus][service]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Register tick callback
    int tick_id = esp_bus_tick(test_svc_handler, 50, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(0, tick_id);
    
    // Wait for some ticks
    vTaskDelay(pdMS_TO_TICKS(180));
    
    // Should have been called ~3 times
    TEST_ASSERT_GREATER_OR_EQUAL(2, test_counter);
    TEST_ASSERT_LESS_OR_EQUAL(4, test_counter);
    
    // Remove tick
    esp_bus_tick_del(tick_id);
    
    int count_after_del = test_counter;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Counter should not have increased much
    TEST_ASSERT_LESS_OR_EQUAL(count_after_del + 1, test_counter);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("esp_bus_after one-shot timer", "[esp_bus][service]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Register one-shot timer
    int timer_id = esp_bus_after(test_svc_handler, 50, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(0, timer_id);
    
    // Before timer fires
    TEST_ASSERT_EQUAL(0, test_counter);
    
    // Wait for timer
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Should have been called exactly once
    TEST_ASSERT_EQUAL(1, test_counter);
    
    // Wait more - should not fire again
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, test_counter);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// LED Module Tests
// ============================================================================

TEST_CASE("esp_bus_led basic operations", "[esp_bus][led]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Register LED (use a safe GPIO for testing)
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_led_reg("led1", &(esp_bus_led_cfg_t){
        .pin = GPIO_NUM_2,
        .active_low = false,
    }));
    
    TEST_ASSERT_TRUE(esp_bus_exists("led1"));
    
    // Test on/off/toggle
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_call("led1.on"));
    vTaskDelay(pdMS_TO_TICKS(20));
    
    uint8_t state = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_req("led1.get_state", NULL, 0, &state, sizeof(state), NULL, 100));
    TEST_ASSERT_EQUAL(1, state);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_call("led1.off"));
    vTaskDelay(pdMS_TO_TICKS(20));
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_req("led1.get_state", NULL, 0, &state, sizeof(state), NULL, 100));
    TEST_ASSERT_EQUAL(0, state);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_call("led1.toggle"));
    vTaskDelay(pdMS_TO_TICKS(20));
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_req("led1.get_state", NULL, 0, &state, sizeof(state), NULL, 100));
    TEST_ASSERT_EQUAL(1, state);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_led_unreg("led1"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Pattern Matching Tests
// ============================================================================

TEST_CASE("pattern matching basic", "[esp_bus][pattern]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    int id1 = esp_bus_sub("*:event1", test_evt_handler, NULL);
    
    esp_bus_emit("any", "event1", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, test_counter);
    
    esp_bus_emit("module", "event1", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(2, test_counter);
    
    esp_bus_emit("any", "event2", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(2, test_counter);  // No match
    
    esp_bus_unsub(id1);
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Memory Leak Tests
// ============================================================================

TEST_CASE("no memory leak on init/deinit cycle", "[esp_bus][memory]")
{
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
    }
    
    MEMORY_CHECK_END(64);  // Allow small tolerance
}

TEST_CASE("no memory leak on module reg/unreg cycle", "[esp_bus][memory]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 10; i++) {
        esp_bus_module_t mod = {
            .name = "test_mod",
            .on_req = test_req_handler,
        };
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("test_mod"));
    }
    
    MEMORY_CHECK_END(64);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on subscribe/unsubscribe cycle", "[esp_bus][memory]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 10; i++) {
        int id = esp_bus_sub("test:*", test_evt_handler, NULL);
        TEST_ASSERT_GREATER_OR_EQUAL(0, id);
        esp_bus_unsub(id);
    }
    
    MEMORY_CHECK_END(64);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on routing on/off cycle", "[esp_bus][memory]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "target",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_on("src:evt", "target.act", NULL, 0));
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_off("src:evt", "target.act"));
    }
    
    MEMORY_CHECK_END(64);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("target"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on routing with data", "[esp_bus][memory]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "target",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    MEMORY_CHECK_START();
    
    char data[] = "test_data_payload";
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_on("src:evt", "target.act", data, sizeof(data)));
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_off("src:evt", "target.act"));
    }
    
    MEMORY_CHECK_END(64);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("target"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on service tick/del cycle", "[esp_bus][memory]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 10; i++) {
        int id = esp_bus_tick(test_svc_handler, 1000, NULL);
        TEST_ASSERT_GREATER_OR_EQUAL(0, id);
        esp_bus_tick_del(id);
    }
    
    MEMORY_CHECK_END(64);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on emit events", "[esp_bus][memory]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    int sub_id = esp_bus_sub("src:*", test_evt_handler, NULL);
    
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 20; i++) {
        char data[32];
        snprintf(data, sizeof(data), "payload_%d", i);
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_emit("src", "event", data, strlen(data) + 1));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    MEMORY_CHECK_END(128);
    
    esp_bus_unsub(sub_id);
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on request with data", "[esp_bus][memory]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    esp_bus_module_t mod = {
        .name = "test",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    MEMORY_CHECK_START();
    
    for (int i = 0; i < 20; i++) {
        char req[32], res[32];
        size_t res_len;
        snprintf(req, sizeof(req), "hello_%d", i);
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_req("test.echo", req, strlen(req) + 1,
                                              res, sizeof(res), &res_len, 100));
    }
    
    MEMORY_CHECK_END(128);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("test"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("no memory leak on LED module", "[esp_bus][memory][led]")
{
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    MEMORY_CHECK_START();
    
    // Note: GPIO driver may allocate some memory on first init
    // Test only 2 cycles, tolerance accounts for GPIO driver overhead
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_led_reg("led1", &(esp_bus_led_cfg_t){
            .pin = GPIO_NUM_2,
        }));
        
        esp_bus_call("led1.on");
        esp_bus_call("led1.off");
        esp_bus_call_s("led1.blink", "100,100,2");
        vTaskDelay(pdMS_TO_TICKS(500));
        
        TEST_ASSERT_EQUAL(ESP_OK, esp_bus_led_unreg("led1"));
    }
    
    // Higher tolerance due to GPIO driver internal allocations
    MEMORY_CHECK_END(256);
    
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

TEST_CASE("heap integrity after stress test", "[esp_bus][memory][stress]")
{
    reset_test_state();
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Register module
    esp_bus_module_t mod = {
        .name = "stress",
        .on_req = test_req_handler,
    };
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_reg(&mod));
    
    // Subscribe
    int sub_id = esp_bus_sub("stress:*", test_evt_handler, NULL);
    
    // Setup route
    esp_bus_on("stress:evt", "stress.action", NULL, 0);
    
    MEMORY_CHECK_START();
    
    // Stress test
    for (int i = 0; i < 50; i++) {
        esp_bus_emit("stress", "evt", NULL, 0);
        esp_bus_call("stress.action");
        
        if (i % 10 == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    MEMORY_CHECK_END(256);
    
    // Verify heap integrity
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    
    esp_bus_off("stress:evt", "stress.action");
    esp_bus_unsub(sub_id);
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_unreg("stress"));
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}

// ============================================================================
// Test Runner
// ============================================================================

void app_main(void)
{
    printf("\n\n=== ESP Bus Unit Tests ===\n\n");
    printf("Free heap: %lu bytes\n\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    
    UNITY_BEGIN();
    
    // Run all tests
    unity_run_all_tests();
    
    UNITY_END();
    
    printf("\nFree heap after tests: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

