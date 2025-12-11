# ESP Bus Unit Tests

Unit tests for the ESP Bus component using Unity test framework.

## Test Categories

| Category | Description |
|----------|-------------|
| `[core]` | Init, deinit, module registration |
| `[request]` | Request/response handling |
| `[event]` | Event emit and subscribe |
| `[routing]` | Event to request routing |
| `[service]` | Tick, timer services |
| `[led]` | LED module operations |
| `[pattern]` | Pattern matching |
| `[memory]` | Memory leak detection |
| `[stress]` | Stress tests with heavy load |

## Running Tests

### Method 1: Flash and Monitor (Recommended)

```bash
cd test
idf.py set-target esp32s3
idf.py build flash monitor
```

### Method 2: Using pytest-embedded

First, install pytest-embedded:

```bash
pip install pytest-embedded pytest-embedded-serial-esp pytest-embedded-idf
```

Then run:

```bash
cd esp_bus
pytest --target esp32s3 -v
pytest --target esp32s3 -k "core"     # Specific category
pytest --target esp32s3 -k "memory"   # Memory tests
```

### Method 3: QEMU (No Hardware)

```bash
pip install pytest-embedded-qemu
pytest --target esp32 --embedded-services esp,idf,qemu -v
```

## Test Output Example

```
=== ESP Bus Unit Tests ===

Running esp_bus_init initializes correctly...
PASS

Running esp_bus_reg registers module...
PASS

Running esp_bus_req sends request to module...
PASS

Running esp_bus_emit and subscribe...
PASS

Running esp_bus_on routes event to request...
PASS

Running esp_bus_tick periodic callback...
PASS

-----------------------
6 Tests 0 Failures 0 Ignored
OK
```

## Adding New Tests

1. Add test function with `TEST_CASE` macro:

```c
TEST_CASE("test description", "[category]")
{
    // Setup
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_init());
    
    // Test
    TEST_ASSERT_TRUE(condition);
    TEST_ASSERT_EQUAL(expected, actual);
    
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, esp_bus_deinit());
}
```

2. Use appropriate assertions:
   - `TEST_ASSERT_EQUAL(expected, actual)`
   - `TEST_ASSERT_TRUE(condition)`
   - `TEST_ASSERT_FALSE(condition)`
   - `TEST_ASSERT_NULL(pointer)`
   - `TEST_ASSERT_NOT_NULL(pointer)`
   - `TEST_ASSERT_EQUAL_STRING(expected, actual)`
   - `TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual)`

## Memory Leak Detection

Memory tests use `heap_caps` API to detect leaks:

```c
// Before operation
size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

// ... operations ...

// After operation
size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
int leaked = heap_before - heap_after;
```

Memory tests include:
- Init/deinit cycles
- Module registration/unregistration
- Subscribe/unsubscribe
- Routing on/off
- Service tick/del
- Event emit stress test
- Request/response stress test
- LED module lifecycle
- Full stress test with heap integrity check

Run only memory tests (requires pytest-embedded):

```bash
pytest --target esp32s3 -k "memory"
```

## CI Integration

Add to your CI pipeline:

```yaml
test:
  script:
    - cd esp_bus
    - idf.py set-target esp32
    - pytest --target esp32 --junitxml=results.xml
  artifacts:
    reports:
      junit: results.xml
```

