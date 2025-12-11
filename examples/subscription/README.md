# ESP Bus Subscription Example

Advanced example demonstrating event subscriptions, wildcards, and custom modules.

## Features

- Wildcard subscription (`btn1:*`) to handle all button events
- Custom module (counter) with request handler
- Event emission from custom module
- Cross-module communication

## How to Use

```bash
idf.py set-target esp32
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

1. **Press button** → Toggle LED (short_press)
2. **Hold >= 1s** → Blink fast (long_press)
3. **Double press** → Blink slow
4. **Press 5 times** → Counter threshold, LED blinks 10 times

## Architecture

```
btn1:short_press ──► on_button_event() ──► led1.toggle
                 └──► counter.inc
                           │
                           ▼
              counter:threshold ──► on_threshold() ──► led1.blink

btn1:long_press ───► on_button_event() ──► led1.blink (fast)
btn1:double_press ─► on_button_event() ──► led1.blink (slow)
```

## Example Output

```
I (300) subscription_example: ESP Bus Subscription Example
I (310) subscription_example: Subscribed to btn1:*
I (320) subscription_example: Subscribed to counter:threshold
I (330) subscription_example: Route: short_press -> counter.inc
I (340) subscription_example: Press button 5 times to trigger threshold event

I (1000) subscription_example: Button event: short_press
I (1000) subscription_example:   -> Toggle LED
I (1000) subscription_example: Counter: 1
I (1100) subscription_example: Button event: short_release

... (repeat 4 more times)

I (5000) subscription_example: Counter: 5
W (5000) subscription_example: Counter reached threshold! Blinking LED...
```
