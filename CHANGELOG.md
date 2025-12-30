# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.3] - 2025-DEC-31

### Fixed

- **Deadlock**: Fix deadlock in `esp_bus_reg()` when checking for existing module (mutex was taken twice due to calling `esp_bus_find_module()` which internally calls `esp_bus_acquire_module()`)

## [1.0.2] - 2025-DEC-31

### Fixed

- **Thread Safety**: Add reference counting for modules, subscriptions, routes, and services
- **Use-after-free**: Fix crash when unregistering module/subscriber during event dispatch
- **Memory Leak**: Fix context memory leak in button and LED module cleanup
- **Buffer Overflow**: Fix action buffer size using ESP_BUS_PATTERN_MAX instead of ESP_BUS_NAME_MAX
- **Request Timeout**: Fix timeout calculation to respect total timeout across queue and response wait
- **Pattern Matching**: Implement `?` wildcard (single character match)

### Changed

- Refactor event dispatch to collect matching handlers before invoking (safer iteration)
- Add `pending_delete` flag for deferred cleanup of in-use nodes

## [1.0.1] - 2025-DEC-12

### Added

- Unit tests with Unity framework (22 test cases)
- Memory leak detection using heap_caps API
- Heap integrity verification after stress tests

## [1.0.0] - 2025-DEC-12

### Added

- Initial release
- Core message bus with request/response pattern
- Event system with publish/subscribe
- Pattern matching with wildcards (`*`, `?`)
- Zero-allocation design with user buffers
- Service loop for lightweight modules (tick, after, every)
- Built-in Button module with debounce, long press, double press detection
- Built-in LED module with on/off/toggle/blink
- Zero-code routing (`esp_bus_on`) to connect events to requests
- Transform functions for dynamic routing
- Kconfig configuration options
- Examples: basic, subscription

### Features

- **Loose Coupling**: Modules only depend on esp_bus
- **Pattern Matching**: String-based routing with wildcards
- **Zero Allocation**: User buffer pattern in hot path
- **Shared Task**: Multiple lightweight modules in single task
- **Schema Support**: Optional action/event schema for validation

