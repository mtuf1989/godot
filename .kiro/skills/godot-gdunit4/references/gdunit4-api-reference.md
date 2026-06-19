# GdUnit4 API Reference

Read this before writing any GdUnit4 test. Covers lifecycle, assertions, test doubles, scene runner, fuzzers, parameterized tests, and skip/error APIs.

## Table of Contents

- [Test Suite Lifecycle](#test-suite-lifecycle)
- [Assertion Factory Methods](#assertion-factory-methods)
- [Assertion Cheat Sheet](#assertion-cheat-sheet)
- [Test Doubles: Mocks](#test-doubles-mocks)
- [Test Doubles: Spies](#test-doubles-spies)
- [Argument Matchers](#argument-matchers)
- [Scene Runner](#scene-runner)
- [Signal Testing](#signal-testing)
- [Error & Warning Testing](#error--warning-testing)
- [Fuzzers](#fuzzers)
- [Parameterized Tests](#parameterized-tests)
- [Skipping Tests](#skipping-tests)
- [Memory Management](#memory-management)
- [Configuration Settings](#configuration-settings)

---

## Test Suite Lifecycle

Every test file extends `GdUnitTestSuite`. Execution order:

```
before()              # once, before all tests in the suite
  before_test()       # before each test
    test_something()  # the test
  after_test()        # after each test
  ...repeat...
after()               # once, after all tests in the suite
```

Minimal test suite:

```gdscript
class_name PlayerTest
extends GdUnitTestSuite

func test_player_starts_with_full_health() -> void:
    var player := auto_free(Player.new()) as Player
    assert_int(player.health).is_equal(100)
```

Rules:
- Test functions MUST start with `test_`.
- Lifecycle hooks are exactly: `before()`, `after()`, `before_test()`, `after_test()`. Wrong names are silently ignored.
- Use `auto_free()` on every object you create to prevent orphan node warnings.
- Each test must be independent. Do not rely on execution order.

---

## Assertion Factory Methods

Each returns a fluent assertion object. Always prefer type-specific over `assert_that()`.

| Method | Returns | Use For |
|--------|---------|---------|
| `assert_that(value)` | `GdUnitAssert` | Generic fallback (only has is_null, is_not_null, is_equal, is_not_equal) |
| `assert_bool(value)` | `GdUnitBoolAssert` | `bool` values |
| `assert_int(value)` | `GdUnitIntAssert` | `int` values |
| `assert_float(value)` | `GdUnitFloatAssert` | `float` values |
| `assert_str(value)` | `GdUnitStringAssert` | `String` values |
| `assert_array(value)` | `GdUnitArrayAssert` | `Array` values |
| `assert_dict(value)` | `GdUnitDictionaryAssert` | `Dictionary` values |
| `assert_object(value)` | `GdUnitObjectAssert` | Object instances |
| `assert_vector(value)` | `GdUnitVectorAssert` | Vector2/2i/3/3i/4/4i |
| `assert_file(path)` | `GdUnitFileAssert` | File existence and content |
| `assert_signal(source)` | `GdUnitSignalAssert` | Signal emission (async) |
| `assert_func(obj, fn)` | `GdUnitFuncAssert` | Polling function return values (async) |
| `assert_result(value)` | `GdUnitResultAssert` | `GdUnitResult` values |
| `assert_error(callable)` | `GdUnitGodotErrorAssert` | push_error, push_warning, runtime errors (async) |
| `assert_failure(callable)` | `GdUnitFailureAssert` | Expecting a test failure |

---

## Assertion Cheat Sheet

### Common (all assertions)

```gdscript
.is_null()
.is_not_null()
.is_equal(expected)
.is_not_equal(expected)
.override_failure_message("custom message")
.append_failure_message("extra context")
```

### Bool

```gdscript
assert_bool(value).is_true()
assert_bool(value).is_false()
```

### Int

```gdscript
assert_int(value).is_equal(42)
assert_int(value).is_not_equal(0)
assert_int(value).is_less(100)
assert_int(value).is_less_equal(100)
assert_int(value).is_greater(0)
assert_int(value).is_greater_equal(0)
assert_int(value).is_between(1, 10)      # inclusive
assert_int(value).is_in([1, 2, 3])
assert_int(value).is_not_in([4, 5, 6])
assert_int(value).is_zero()
assert_int(value).is_not_zero()
assert_int(value).is_odd()
assert_int(value).is_even()
assert_int(value).is_negative()
assert_int(value).is_not_negative()
```

### Float

```gdscript
assert_float(value).is_equal(3.14)
assert_float(value).is_equal_approx(3.14, 0.01)  # within ±tolerance
assert_float(value).is_less(10.0)
assert_float(value).is_greater(0.0)
assert_float(value).is_between(0.0, 1.0)
assert_float(value).is_zero()
assert_float(value).is_not_zero()
assert_float(value).is_negative()
assert_float(value).is_not_negative()
```

### String

```gdscript
assert_str(value).is_equal("expected")
assert_str(value).is_not_equal("other")
assert_str(value).is_empty()
assert_str(value).is_not_empty()
assert_str(value).contains("sub")
assert_str(value).not_contains("bad")
assert_str(value).starts_with("prefix")
assert_str(value).ends_with("suffix")
assert_str(value).has_length(5)
assert_str(value).has_length(5, Comparator.LESS_THAN)  # length < 5
assert_str(value).is_equal_ignoring_case("HELLO")
assert_str(value).contains_ignoring_case("TEST")
```

### Array

Arguments are variadic (individual elements), NOT wrapped in an array:

```gdscript
assert_array(value).is_empty()
assert_array(value).is_not_empty()
assert_array(value).has_size(3)
assert_array(value).contains(1, 3)                         # contains these (any order)
assert_array(value).contains_exactly(1, 2, 3)              # exact elements, exact order
assert_array(value).contains_exactly_in_any_order(3, 1, 2) # exact set, any order
assert_array(value).not_contains(4, 5)
```

#### Value Extraction (for object arrays)

```gdscript
# Extract a single property/method
assert_array(nodes).extract("get_name").contains_exactly("Player", "Enemy")

# Extract multiple into tuples
assert_array(items)\
    .extractv(extr("get_name"), extr("get_price"))\
    .contains_exactly(tuple("Sword", 100), tuple("Shield", 50))

# Chained extraction (dot notation)
assert_array(nodes).extractv(extr("get_parent.get_name")).contains("Root")
```

### Dictionary

```gdscript
assert_dict(value).is_empty()
assert_dict(value).is_not_empty()
assert_dict(value).has_size(2)
assert_dict(value).contains_keys("key1", "key2")
assert_dict(value).not_contains_keys("bad_key")
assert_dict(value).contains_key_value("key", "value")
```

### Object

```gdscript
assert_object(value).is_instanceof(Player)
assert_object(value).is_not_instanceof(Enemy)
assert_object(value).is_same(other_ref)       # reference identity
assert_object(value).is_not_same(other_ref)
assert_object(value).is_inheriting(Node)      # walks inheritance chain
```

### Vector

Works with Vector2, Vector2i, Vector3, Vector3i, Vector4, Vector4i:

```gdscript
assert_vector(Vector2(1, 2)).is_equal(Vector2(1, 2))
assert_vector(Vector3(1, 2, 3)).is_equal_approx(Vector3(1.01, 2.01, 3.01), Vector3(0.1, 0.1, 0.1))
assert_vector(Vector2(5, 5)).is_between(Vector2(0, 0), Vector2(10, 10))
assert_vector(Vector2(5, 5)).is_greater(Vector2(1, 1))
```

### File

```gdscript
assert_file("res://project.godot").exists()
assert_file("res://my_script.gd").is_file()
```

### Result

```gdscript
assert_result(result).is_success()
assert_result(result).is_error()
assert_result(result).contains_message("error text")
```

---

## Test Doubles: Mocks

Mocks create controlled stand-ins. **Default mode is `RETURN_DEFAULTS`** (returns type defaults: 0, "", false, null, Vector2.ZERO, etc.).

### Creating Mocks

```gdscript
# Mock by class type (RETURN_DEFAULTS mode)
var m: Variant = mock(Node)
var m: Variant = mock(MyCustomClass)

# Mock by script path
var m: Variant = mock("res://src/player.gd")

# Mock by scene path (always uses CALL_REAL_FUNC mode)
var m: Variant = mock("res://scenes/player.tscn")

# Mock with CALL_REAL_FUNC mode — calls real implementation unless stubbed
var m: Variant = mock(MyClass, CALL_REAL_FUNC)
```

### Stubbing Return Values

```gdscript
do_return(42).on(m).get_child_count()
do_return("Alice").on(m).get_meta("name")
do_return(100).on(m).get_meta("health")
```

### Verifying Interactions

```gdscript
verify(m, 2).get_child_count()       # called exactly 2 times
verify(m, 1).set_name("test")        # called exactly 1 time
verify(m, 0).get_child(0)            # never called
verify_no_interactions(m)            # no calls at all
verify_no_more_interactions(m)       # all calls have been verified
reset(m)                             # reset interaction history
```

### Mock Constraints

- **Cannot mock instances.** `mock(Node.new())` fails. Pass the class: `mock(Node)`.
- **Cannot stub void functions.** `do_return("x").on(m).void_method()` errors.
- **`do_nothing()` does NOT exist.** Mocks in RETURN_DEFAULTS already do nothing.
- **Cannot mock singletons.** `mock(Input)` fails.
- **Scene mocks require a script.** `.tscn` without a script cannot be mocked.
- **Variadic functions limited to 10 args.**

---

## Test Doubles: Spies

Spies wrap **real instances**, preserving behavior while tracking calls.

```gdscript
# Spy on an instance (MUST be an instance, not a class)
var node := auto_free(Node.new())
var spy_node: Variant = spy(node)

# Spy on a scene by path
var spy_scene: Variant = spy("res://scenes/player.tscn")

# Spy on a PackedScene
var spy_scene: Variant = spy(load("res://scenes/player.tscn"))

# Spy on a Callable
var cb := func(x: int) -> String: return "value_%d" % x
var cb_spy: Variant = spy(cb)
cb_spy.call(42)
verify(cb_spy).call(42)
```

### Spy Constraints

- **Cannot spy on a class.** `spy(Node)` returns null. Pass an instance.
- **Spy copies properties at creation time.** Modify the instance before creating the spy.
- **Input callbacks (`_input`, `_gui_input`, `_unhandled_input`) are excluded** from spy real calls.

---

## Argument Matchers

Use in `verify()` and `do_return()` calls:

| Matcher | Matches |
|---------|---------|
| `any()` | Any value of any type |
| `any_bool()` | Any boolean |
| `any_int()` | Any integer |
| `any_float()` | Any float |
| `any_string()` | Any String |
| `any_color()` | Any Color |
| `any_vector()` | Any Vector type (2/2i/3/3i/4/4i) |
| `any_vector2()` | Vector2 specifically |
| `any_vector3()` | Vector3 specifically |
| `any_object()` | Any Object |
| `any_dictionary()` | Any Dictionary |
| `any_array()` | Any Array |
| `any_node_path()` | Any NodePath |
| `any_rid()` | Any RID |
| `any_class(MyClass)` | Any instance of the given class |
| `any_packed_byte_array()` | Any PackedByteArray |
| `any_packed_int32_array()` | Any PackedInt32Array |
| `any_packed_string_array()` | Any PackedStringArray |

### Custom Argument Matchers

```gdscript
class GreaterThanMatcher extends GdUnitArgumentMatcher:
    var _threshold: int

    func _init(threshold: int) -> void:
        _threshold = threshold

    func is_match(value: Variant) -> bool:
        return value is int and value > _threshold

# Usage
verify(m, 1).set_health(GreaterThanMatcher.new(50))
```

---

## Scene Runner

Loads a scene into the tree for integration testing.

### Loading

```gdscript
# By resource path (most common — auto-freed)
var runner := scene_runner("res://scenes/game.tscn")

# By UID
var runner := scene_runner("uid://cn8ucy2rheu0f")

# By pre-instantiated node (YOU must auto_free it)
var scene := auto_free(load("res://scenes/game.tscn").instantiate())
var runner := scene_runner(scene)

# With a spy for interaction verification
var spy_scene: Variant = spy("res://scenes/game.tscn")
var runner := scene_runner(spy_scene)
```

### Accessing the Scene

```gdscript
var scene_node: Node = runner.scene()
var player: Node = runner.find_child("Player")
var health: int = runner.get_property("_health")
runner.set_property("_health", 100)
var result: Variant = runner.invoke("calculate_damage", 50, 0.8)  # direct args, NOT array
```

### Keyboard Input

```gdscript
# Async tap (press + frame + release) — MUST await
await runner.simulate_key_pressed(KEY_SPACE)

# Hold a key down (sync)
runner.simulate_key_press(KEY_SHIFT)

# Release a held key (sync)
runner.simulate_key_release(KEY_SHIFT)
```

### Mouse Input

```gdscript
runner.set_mouse_position(Vector2(100, 200))
runner.simulate_mouse_button_pressed(MOUSE_BUTTON_LEFT)          # click
runner.simulate_mouse_button_pressed(MOUSE_BUTTON_LEFT, true)    # double click
runner.simulate_mouse_button_press(MOUSE_BUTTON_LEFT)            # hold
runner.simulate_mouse_button_release(MOUSE_BUTTON_LEFT)          # release

# Smooth animated movement
await runner.simulate_mouse_move_relative(Vector2(100, 0), 0.5)
await runner.simulate_mouse_move_absolute(Vector2(200, 200), 1.0)
```

### Touch Input

```gdscript
runner.simulate_screen_touch_pressed(0, Vector2(100, 100))
runner.simulate_screen_touch_press(0, Vector2(100, 100))
runner.simulate_screen_touch_release(0)
await runner.simulate_screen_touch_drag_drop(0, Vector2(50, 50), Vector2(200, 50))
```

### Action Input

```gdscript
runner.simulate_action_pressed("ui_accept")    # tap
runner.simulate_action_press("ui_up")          # hold
runner.simulate_action_release("ui_up")        # release
```

### Frame Simulation & Timing

```gdscript
await runner.simulate_frames(10)
await runner.simulate_frames(10, 50)   # 10 frames, 50ms each
runner.set_time_factor(5.0)            # speed up (max 9.0x)

# Run until signal
await runner.simulate_until_signal("animation_finished")
await runner.simulate_until_object_signal(enemy, "died", enemy)
```

### Awaiting

```gdscript
await runner.await_signal("level_complete", [3], 5000)
await runner.await_signal_on(door, "opened", [], 2000)
await runner.await_func("get_state").is_equal("idle")
await runner.await_func("get_health").wait_until(5000).is_equal(100)
await runner.await_func_on(player, "is_grounded").is_true()
await runner.await_input_processed()
```

---

## Signal Testing

**You must call `monitor_signals()` before asserting signals.**

```gdscript
func test_signal_emitted() -> void:
    var emitter := monitor_signals(auto_free(MyEmitter.new()))
    emitter.do_something()
    await assert_signal(emitter).is_emitted("something_happened")

# With specific arguments
await assert_signal(emitter).is_emitted("value_changed", 42, "health")

# Assert NOT emitted (waits full timeout)
await assert_signal(emitter).is_not_emitted("my_signal")

# Custom timeout
await assert_signal(emitter).wait_until(5000).is_emitted("slow_signal")

# Check signal exists (synchronous)
assert_signal(emitter).is_signal_exists("my_signal")
```

**Critical:** `is_emitted` and `is_not_emitted` are async. You MUST `await` them. Without await, they always pass silently.

---

## Error & Warning Testing

All `assert_error` methods are async — always `await`.

```gdscript
# Test push_error()
await assert_error(func() -> void:
    push_error("something went wrong")
).is_push_error("something went wrong")

# Test push_warning()
await assert_error(func() -> void:
    push_warning("deprecated usage")
).is_push_warning("deprecated usage")

# Test runtime errors
await assert_error(func() -> void:
    @warning_ignore("integer_division")
    var _x: int = 1 / 0
).is_runtime_error("Division by zero error in operator '/'.")

# Test no error occurs
await assert_error(func() -> void:
    var _x := 1 + 1
).is_success()
```

---

## Fuzzers

### Built-in Fuzzers

| Factory | Description |
|---------|-------------|
| `Fuzzers.rangei(from, to)` | Random integers in range |
| `Fuzzers.rangef(from, to)` | Random floats in range |
| `Fuzzers.rangev2(from, to)` | Random Vector2 in range |
| `Fuzzers.rangev3(from, to)` | Random Vector3 in range |
| `Fuzzers.eveni(from, to)` | Even integers only |
| `Fuzzers.oddi(from, to)` | Odd integers only |
| `Fuzzers.boolean()` | Random true/false |
| `Fuzzers.rand_str(min, max, charset)` | Random strings (charset: `"a-zA-Z0-9!@#"`) |

### Usage

```gdscript
func test_value_in_range(
    fuzzer := Fuzzers.rangei(-100, 100),
    fuzzer_iterations := 500,
    fuzzer_seed := 12345
) -> void:
    assert_int(fuzzer.next_value()).is_between(-100, 100)
```

Default is 1000 iterations. Argument names must be exactly `fuzzer` (or `fuzzer_a`, `fuzzer_b`), `fuzzer_iterations`, `fuzzer_seed`.

### Custom Fuzzer

```gdscript
class EdgeCaseFuzzer extends Fuzzer:
    var _values := [0, -1, 1, -2147483648, 2147483647]
    var _index := 0

    func next_value() -> Variant:
        var val = _values[_index % _values.size()]
        _index += 1
        return val

func test_edge_cases(fuzzer := EdgeCaseFuzzer.new(), fuzzer_iterations := 5) -> void:
    var value: int = fuzzer.next_value()
    # test logic
```

**Trap:** Fuzz tests stop on first failure. `before_test()`/`after_test()` run for EVERY iteration.

---

## Parameterized Tests

```gdscript
func test_is_even(value: int, expected: bool, test_parameters := [
    [2, true],
    [3, false],
    [0, true],
    [-4, true],
]) -> void:
    assert_bool(value % 2 == 0).is_equal(expected)
```

- Parameter name MUST be `test_parameters` (or `_test_parameters`). Any other name silently skips the test.
- Parameter types must match function argument types.

---

## Skipping Tests

Use `do_skip` and `skip_reason` as default arguments:

```gdscript
func test_windows_only(
    do_skip := OS.get_name() != "Windows",
    skip_reason := "Only runs on Windows"
) -> void:
    # ...

func test_not_implemented(do_skip := true, skip_reason := "TODO") -> void:
    assert_not_yet_implemented()
```

Skip entire suite via `before()`:

```gdscript
func before(do_skip := true, skip_reason := "Suite disabled") -> void:
    pass
```

**Trap:** Unknown test arguments (typos in `do_skip`, `skip_reason`, `test_parameters`, `fuzzer_iterations`, `fuzzer_seed`, `timeout`) cause silent test skips.

---

## Memory Management

```gdscript
# auto_free registers for cleanup after the test
var node := auto_free(Node2D.new())

# Temp files (auto-cleaned after suite's after() hook)
var dir := create_temp_dir("my_test")
var file := create_temp_file("my_test", "data.txt")

# Debug orphans
collect_orphan_node_details()
```

Rules:
- `before()` allocations → free in `after()`
- `before_test()` allocations → free in `after_test()`
- `scene_runner()` auto-frees scenes loaded by path. Don't double-wrap.
- Mock and spy instances are auto-freed by the framework.
- Source instances for spies need `auto_free()`: `spy(auto_free(Node.new()))`

---

## Configuration Settings

Key settings under `gdunit4/` in ProjectSettings:

| Setting | Default | Purpose |
|---------|---------|---------|
| `settings/test/test_timeout_seconds` | `300` (5 min) | Per-test timeout |
| `settings/test/test_lookup_folder` | `"test"` | Root folder for test discovery |
| `settings/test/test_discovery` | `false` | Auto-discover tests on script save |
| `settings/test/flaky_check_enable` | `false` | Retry failed tests |
| `settings/test/flaky_max_retries` | `3` | Max retries for flaky tests |
| `report/godot/push_error` | `false` | Treat push_error as test failure |
| `report/godot/script_error` | `true` | Treat script errors as test failure |
| `report/verbose_orphans` | `true` | Report orphaned nodes |
| `report/assert/strict_number_type_compare` | `true` | Strict int/float comparison |

Per-test timeout override:

```gdscript
func test_slow_operation(timeout := 10000) -> void:  # 10 seconds
    await long_running_operation()
```

**Trap:** `push_error()` does NOT fail tests by default. Enable `report/godot/push_error` or use `assert_error()` explicitly.

**Trap:** `strict_number_type_compare` is on by default. `assert_float(1).is_equal(1.0)` may fail. Use matching types.
