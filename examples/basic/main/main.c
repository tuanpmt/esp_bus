/**
 * @file main.c
 * @brief ESP Bus Basic Example - Button controls LED
 * 
 * This example demonstrates:
 * - Initializing esp_bus
 * - Registering button and LED modules
 * - Using zero-code routing to connect button events to LED actions
 * - Subscribing to events for logging
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_bus.h"
#include "esp_bus_btn.h"
#include "esp_bus_led.h"

static const char *TAG = "basic";

// GPIO Configuration - adjust for your board
#define BUTTON_GPIO     GPIO_NUM_0      // Boot button on most ESP32 boards
#define LED_GPIO        GPIO_NUM_2      // Built-in LED on most ESP32 boards

/**
 * @brief Button event handler - logs all button events
 */
static void on_button(const char *evt, const void *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "Button: %s", evt);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP Bus Basic Example");
    
    // Initialize bus
    ESP_ERROR_CHECK(esp_bus_init());
    
    // Register button (active low, with internal pull-up)
    ESP_ERROR_CHECK(esp_bus_btn_reg("btn1", &(esp_bus_btn_cfg_t){
        .pin = BUTTON_GPIO,
        .active_low = true,
        .long_press_ms = 1000,
        .double_press_ms = 300,
    }));
    ESP_LOGI(TAG, "Button registered on GPIO%d", BUTTON_GPIO);
    
    // Register LED
    ESP_ERROR_CHECK(esp_bus_led_reg("led1", &(esp_bus_led_cfg_t){
        .pin = LED_GPIO,
        .active_low = false,
    }));
    ESP_LOGI(TAG, "LED registered on GPIO%d", LED_GPIO);
    
    // Subscribe to all button events for logging
    esp_bus_sub("btn1:*", on_button, NULL);
    
    // Zero-code routing: connect button events to LED actions
    
    // Short press → Toggle LED
    esp_bus_on(BTN_ON_SHORT("btn1"), LED_CMD_TOGGLE("led1"), NULL, 0);
    
    // Long press → Blink 3 times
    esp_bus_on(BTN_ON_LONG("btn1"), LED_CMD_BLINK("led1"), "100,100,3", 10);
    
    // Double press → Fast blink forever
    esp_bus_on(BTN_ON_DOUBLE("btn1"), LED_CMD_BLINK("led1"), "50,50,-1", 9);
    
    ESP_LOGI(TAG, "Routes configured:");
    ESP_LOGI(TAG, "  - Short press -> Toggle");
    ESP_LOGI(TAG, "  - Long press -> Blink 3x");
    ESP_LOGI(TAG, "  - Double press -> Fast blink");
    
    // Main loop - nothing to do, esp_bus handles everything
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
