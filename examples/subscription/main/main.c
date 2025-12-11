/**
 * @file main.c
 * @brief ESP Bus Subscription Example
 * 
 * This example demonstrates:
 * - Subscribing to events with callbacks
 * - Using wildcards to match multiple events
 * - Creating custom modules
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_bus.h"
#include "esp_bus_btn.h"
#include "esp_bus_led.h"

static const char *TAG = "subscription_example";

// GPIO Configuration
#define BUTTON_GPIO     GPIO_NUM_0
#define LED_GPIO        GPIO_NUM_2

//-----------------------------------------------------------------------------
// Event Handlers
//-----------------------------------------------------------------------------

/**
 * @brief Handle all button events using wildcard subscription
 */
static void on_button_event(const char *evt, const void *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "Button event: %s", evt);
    
    if (strcmp(evt, BTN_SHORT) == 0) {
        ESP_LOGI(TAG, "  -> Toggle LED");
        esp_bus_call(LED_CMD_TOGGLE("led1"));
    } 
    else if (strcmp(evt, BTN_LONG) == 0) {
        ESP_LOGI(TAG, "  -> Blink fast");
        esp_bus_call_s(LED_CMD_BLINK("led1"), "100,100,-1");
    }
    else if (strcmp(evt, BTN_DOUBLE) == 0) {
        ESP_LOGI(TAG, "  -> Blink slow");
        esp_bus_call_s(LED_CMD_BLINK("led1"), "500,500,-1");
    }
}

//-----------------------------------------------------------------------------
// Custom Module: Counter
//-----------------------------------------------------------------------------

typedef struct {
    int count;
    int threshold;
} counter_ctx_t;

static counter_ctx_t counter_ctx = {
    .count = 0,
    .threshold = 5,
};

static esp_err_t counter_handler(const char *action,
                                  const void *req, size_t req_len,
                                  void *res, size_t res_size, size_t *res_len,
                                  void *ctx)
{
    counter_ctx_t *cnt = (counter_ctx_t *)ctx;
    
    if (strcmp(action, "inc") == 0) {
        cnt->count++;
        ESP_LOGI(TAG, "Counter: %d", cnt->count);
        
        if (cnt->count >= cnt->threshold) {
            esp_bus_emit("counter", "threshold", &cnt->count, sizeof(cnt->count));
            cnt->count = 0;
        }
        return ESP_OK;
    }
    
    if (strcmp(action, "reset") == 0) {
        cnt->count = 0;
        ESP_LOGI(TAG, "Counter reset");
        return ESP_OK;
    }
    
    if (strcmp(action, "get") == 0) {
        if (res && res_size >= sizeof(int)) {
            *(int *)res = cnt->count;
            if (res_len) *res_len = sizeof(int);
        }
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_SUPPORTED;
}

/**
 * @brief Handle counter threshold event
 */
static void on_threshold(const char *evt, const void *data, size_t len, void *ctx)
{
    ESP_LOGW(TAG, "Counter reached threshold! Blinking LED...");
    esp_bus_call_s(LED_CMD_BLINK("led1"), "50,50,10");
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "ESP Bus Subscription Example");
    
    // Initialize
    ESP_ERROR_CHECK(esp_bus_init());
    
    // Register hardware modules
    ESP_ERROR_CHECK(esp_bus_btn_reg("btn1", &(esp_bus_btn_cfg_t){
        .pin = BUTTON_GPIO,
        .active_low = true,
    }));
    
    ESP_ERROR_CHECK(esp_bus_led_reg("led1", &(esp_bus_led_cfg_t){
        .pin = LED_GPIO,
    }));
    
    // Register custom counter module
    ESP_ERROR_CHECK(esp_bus_reg(&(esp_bus_module_t){
        .name = "counter",
        .on_req = counter_handler,
        .ctx = &counter_ctx,
    }));
    
    //-------------------------------------------------------------------------
    // Subscriptions
    //-------------------------------------------------------------------------
    
    // Subscribe to ALL button events using wildcard
    esp_bus_sub("btn1:*", on_button_event, NULL);
    ESP_LOGI(TAG, "Subscribed to btn1:*");
    
    // Subscribe to counter threshold event
    esp_bus_sub("counter:threshold", on_threshold, NULL);
    ESP_LOGI(TAG, "Subscribed to counter:threshold");
    
    //-------------------------------------------------------------------------
    // Routes
    //-------------------------------------------------------------------------
    
    // Button short press → increment counter
    esp_bus_on(BTN_ON_SHORT("btn1"), "counter.inc", NULL, 0);
    ESP_LOGI(TAG, "Route: short_press → counter.inc");
    
    ESP_LOGI(TAG, "Press button %d times to trigger threshold event", counter_ctx.threshold);
    
    // Keep running
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}

