# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
- Unit tests with Unity framework (22 test cases)
- Memory leak detection using heap_caps API
- Heap integrity verification after stress tests

### Features

- **Loose Coupling**: Modules only depend on esp_bus
- **Pattern Matching**: String-based routing with wildcards
- **Zero Allocation**: User buffer pattern in hot path
- **Shared Task**: Multiple lightweight modules in single task
- **Schema Support**: Optional action/event schema for validation

