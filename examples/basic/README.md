# ESP Bus Basic Example

Simple example demonstrating button and LED control using ESP Bus.

## Hardware Required

- ESP32 development board
- Button connected to GPIO0 (or use the built-in BOOT button)
- LED connected to GPIO2 (or use the built-in LED)

## How to Use

### Configure GPIO (Optional)

Edit `main/main.c` to change GPIO pins:

```c
#define BUTTON_GPIO     GPIO_NUM_0
#define LED_GPIO        GPIO_NUM_2
```

### Build and Flash

```bash
idf.py set-target esp32   # or esp32s3, esp32c3, etc.
idf.py build
idf.py flash monitor
```

## Button Events

| Event | When |
|-------|------|
| `short_press` | Immediately on press |
| `long_press` | While held >= 1 second |
| `short_release` | Released before long_press |
| `long_release` | Released after long_press |
| `double_press` | Double click |

## Expected Behavior

| Action | Result |
|--------|--------|
| Press button | Toggle LED (short_press) |
| Hold >= 1s | Blink 3 times (long_press) |
| Double press | Fast blink continuously |

## Example Output

```
I (300) basic: ESP Bus Basic Example
I (310) esp_bus: Initialized
I (320) esp_bus_btn: Registered 'btn1' on GPIO0
I (330) esp_bus_led: Registered 'led1' on GPIO2
I (340) basic: Routes configured:
I (350) basic:   - Short press -> Toggle
I (360) basic:   - Long press -> Blink 3x
I (370) basic:   - Double press -> Fast blink

I (1000) basic: Button: short_press
I (1100) basic: Button: short_release
I (2000) basic: Button: short_press
I (3000) basic: Button: long_press
I (3500) basic: Button: long_release
```
