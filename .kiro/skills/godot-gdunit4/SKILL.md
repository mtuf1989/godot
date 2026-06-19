---
name: godot-gdunit4
description: |
  Write, organize, and run GdUnit4 unit tests and integration tests for Godot 4 projects.
  Use when the user needs to create test suites, write test cases, add assertions, mock or spy on objects,
  test scenes with scene_runner, write fuzz tests, set up CI test pipelines, or debug failing tests.
  Also use when someone mentions GdUnit4, GdUnitTestSuite, assert_that, assert_str, assert_array,
  assert_signal, assert_vector, assert_error, scene_runner, mock(), spy(), auto_free, monitor_signals,
  test_timeout, parameterized tests, fuzz testing in Godot, test discovery, GdUnit4 assertions,
  or asks to "write tests," "add unit tests," "test this system," "verify this works,"
  "add test coverage," or "set up CI testing."
  Even if the user just says "test this" or "make sure this doesn't break" in a Godot project
  that has GdUnit4 installed, this skill applies.
  Do NOT use for architecture decisions (use godot-architect), general GDScript implementation
  without testing (use godot-gdscript), or UI layout work (use godot-ui-core).
---

# Godot GdUnit4

Write and run GdUnit4 tests for Godot 4 projects with correct assertion APIs, proper test lifecycle, validated mocks/spies, and honest reporting of test results.

Read `references/gdunit4-api-reference.md` before writing any test. Read `references/gdunit4-test-patterns.md` before structuring test suites or setting up CI.

## Responsibility

- Write test suites extending `GdUnitTestSuite` with typed GDScript.
- Use the correct assertion methods for each value type (bool, int, float, string, array, dict, object, vector, signal, file, result, error).
- Create mocks and spies using `mock()` and `spy()` with proper verification.
- Write scene integration tests using `scene_runner()`.
- Write parameterized tests and fuzz tests when appropriate.
- Test signals using `monitor_signals()` + `assert_signal()`.
- Test errors/warnings using `assert_error()`.
- Organize test files to mirror source structure.
- Run tests and interpret results using editor mode or CLI.
- Set up CI pipelines using the GdUnit4 GitHub Action.

## Workflow

1. Confirm GdUnit4 is installed by checking for `addons/gdUnit4/plugin.cfg`. If missing, offer to install it.

2. Read `references/gdunit4-api-reference.md` to ground yourself in the exact API.

3. Identify what needs testing:
   - **Unit tests**: Pure logic, data transformations. Assert return values directly.
   - **Integration tests**: Scene behavior, node interactions. Use `scene_runner()`.
   - **Mock/spy tests**: Isolate dependencies. Use `mock()` for stand-ins, `spy()` for observing real objects.
   - **Fuzz tests**: Boundary/stress testing. Use `Fuzzers` for random inputs.
   - **Parameterized tests**: Same logic, multiple inputs. Use `test_parameters`.

4. Determine test file location:
   - Mirror source structure under the test lookup folder (default: `test/`).
   - Test file names end with `_test.gd`.
   - Each file has one class extending `GdUnitTestSuite` with a `class_name`.

5. Write the test suite:
   - Extend `GdUnitTestSuite`. Add `class_name` and `const __source`.
   - Use `before()` / `after()` for suite-level setup/teardown.
   - Use `before_test()` / `after_test()` for per-test setup/teardown.
   - Name test functions starting with `test_`.
   - Use `auto_free()` for any objects created during tests.
   - Use the correct typed assertion (not `assert_that()` when a specific one exists).
   - Always `await` async assertions: signal, error, func polling, `simulate_key_pressed`.

6. For scene integration tests:
   - Create a runner: `var runner := scene_runner("res://path/to/scene.tscn")`
   - Simulate key tap (async): `await runner.simulate_key_pressed(KEY_SPACE)`
   - Hold a key (sync): `runner.simulate_key_press(KEY_RIGHT)` / `runner.simulate_key_release(KEY_RIGHT)`
   - Advance frames: `await runner.simulate_frames(10)`
   - Access state: `runner.get_property("health")`, `runner.invoke("method", arg1, arg2)`
   - Wait for signals: `await runner.await_signal("died", [], 2000)`
   - Speed up: `runner.set_time_factor(5.0)` (max 9.0)

7. For mocks and spies:
   - Create a mock: `var m: Variant = mock(MyClass)` — default mode is RETURN_DEFAULTS.
   - Stub: `do_return(42).on(m).get_value()`
   - Verify: `verify(m, 1).get_value()`
   - Create a spy: `var s: Variant = spy(auto_free(real_instance))`
   - Spy preserves real behavior but records calls for verification.
   - Use argument matchers: `any_int()`, `any_string()`, `any_class(MyClass)`, etc.

8. For signal testing:
   - Always call `monitor_signals()` first: `var obj := monitor_signals(auto_free(MyObj.new()))`
   - Then trigger the action, then await the assertion: `await assert_signal(obj).is_emitted("sig")`

9. Write files to disk. After writing `.gd` test files, call `refresh_filesystem`.

10. Validate:
    - Run `get_diagnostics` on all new/changed test scripts.
    - Suggest running tests from the GdUnit4 Inspector panel or CLI.
    - If tests fail, read failure messages carefully — GdUnit4 provides detailed diffs.

## Style Rules

**Group related tests with `#region`:**

```gdscript
#region take_damage
func test_take_damage_reduces_health() -> void:
    assert_int(stats.health).is_equal(80)

func test_take_damage_cannot_go_below_zero() -> void:
    assert_int(stats.health).is_equal(0)
#endregion
```

**Prefer whole-object comparison over field-by-field:**

```gdscript
# Preferred — one assertion, full comparison
assert_that(result).is_equal(PlayerStats.new("Hero", 100))

# Avoid — noisy, fragile, easy to miss a property
assert_str(result.name).is_equal("Hero")
assert_int(result.health).is_equal(100)
```

Only fall back to field-by-field when verifying a single property in isolation or when the expected object cannot be easily constructed.

**Inline expected values — avoid unnecessary variables:**

```gdscript
# Preferred
assert_str(player.name).is_equal("Hero")

# Avoid — variable adds no value
var expected_name := "Hero"
assert_str(player.name).is_equal(expected_name)
```

**Fluent chain line breaks:**

Keep chains on one line when under 140 characters with a single validation. Break with `\` when exceeding 140 chars or chaining multiple validations:

```gdscript
# One line — short, single validation
assert_bool(player.is_alive).is_true()

# Break — multiple validations chained
assert_str(player.name) \
    .is_not_empty() \
    .starts_with("H") \
    .is_equal("Hero")
```

## Quality Gates

- All test files extend `GdUnitTestSuite` with `class_name`.
- Test functions start with `test_`.
- Type-specific assertions used (not generic `assert_that`).
- All created objects use `auto_free()`.
- **All async assertions are awaited** (signal, error, func, simulate_key_pressed).
- `monitor_signals()` called before any signal assertion.
- Mocks declared as `Variant` type (not cast to the mocked class).
- Mock default mode understood as RETURN_DEFAULTS.
- Scene runner `invoke()` uses direct args, not array.
- Parameterized tests use exactly `test_parameters` as the argument name.
- Fuzz tests specify `fuzzer_iterations` and optionally `fuzzer_seed`.
- Test file organization mirrors source structure.
- `refresh_filesystem` called after disk writes.
- Typed GDScript throughout.
- For balance/tuning tests, encode expected curves as assertions (DPS thresholds, TTK ranges, economy affordability) — these prevent balance regressions automatically.
- No false claims about test results.

## Failure Modes

- Do not invent assertion methods that do not exist. Refer to `references/gdunit4-api-reference.md`.
- Do not use `do_nothing()` — it does not exist. RETURN_DEFAULTS mode already returns defaults.
- Do not cast mock/spy results to the mocked type. Use `var m: Variant = mock(...)`.
- Do not pass arrays to `invoke()`. Use direct args: `runner.invoke("method", arg1, arg2)`.
- Do not forget `await` on async operations. This is the #1 source of false-passing tests.
- Do not assert signals without calling `monitor_signals()` first.
- Do not use `assert_that()` when a type-specific assertion exists.
- Do not forget `auto_free()` on created objects.
- Do not use `:=` type inference with `auto_free()` — it returns `Variant`, not the typed class. In projects with warnings-as-errors this causes compile failures. Always use explicit type: `var x: MyClass = auto_free(MyClass.new())`.
- Do not mock instances (`mock(Node.new())` fails) — pass the class.
- Do not spy on classes (`spy(Node)` returns null) — pass an instance.
- Do not confuse `simulate_key_press` (hold, sync) with `simulate_key_pressed` (tap, async).
- Do not assume `push_error()` fails tests — it doesn't by default.
- Do not claim tests passed if they were not actually executed.
- Do not write tests that depend on execution order.
- Do not instantiate scripts with `@onready` vars or autoload references via `set_script()` or `load().new()` without their full scene tree. The `@onready` resolution or `_ready()` autoload calls will crash (SIGSEGV or "Nonexistent function 'new'"). For scripts that extend CanvasLayer/Control/Node with scene-dependent `_ready()`: either use `scene_runner("res://path.tscn")` for integration, or inline the pure logic being tested directly in the test class for unit testing.
- Do not use lambda signal connections on `auto_free()` objects to verify signal emission — the lambda never fires. Instead, verify the signal's side effects via state changes (e.g., assert the gold value increased rather than asserting the `gold_changed` signal emitted). If you must test signal emission directly, use `scene_runner()` integration tests with `await_signal()`.

## References

Read only as needed:

- `references/gdunit4-api-reference.md` — assertion API, test doubles, scene runner, fuzzers, lifecycle, config
- `references/gdunit4-test-patterns.md` — test organization, patterns, CI setup, CLI, hidden traps
