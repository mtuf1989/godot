# GdUnit4 Test Patterns

Read this before structuring test suites, organizing test files, or setting up CI.

## Table of Contents

- [Test File Organization](#test-file-organization)
- [Common Test Patterns](#common-test-patterns)
- [Scene Integration Testing](#scene-integration-testing)
- [Signal Testing Patterns](#signal-testing-patterns)
- [Error & Warning Testing](#error--warning-testing)
- [Mock Patterns](#mock-patterns)
- [Spy Patterns](#spy-patterns)
- [Testing Resources](#testing-resources)
- [Testing State Machines](#testing-state-machines)
- [Memory Management Best Practices](#memory-management-best-practices)
- [CI Setup](#ci-setup)
- [CLI Usage](#cli-usage)
- [Hidden Traps & Gotchas](#hidden-traps--gotchas)
- [Performance Tips](#performance-tips)

---

## Test File Organization

Tests mirror the source directory structure:

```
project/
  scripts/
    player/
      player_controller.gd
      player_stats.gd
    enemies/
      enemy_base.gd
  test/                              # default test lookup folder
    scripts/
      player/
        player_controller_test.gd
        player_stats_test.gd
      enemies/
        enemy_base_test.gd
```

Rules:
- One test suite per source file.
- Test file name = source file name + `_test.gd`.
- Always add `class_name` to test suites.
- Set `const __source` pointing to the source file being tested.
- Use `#region` / `#endregion` to group related tests.

---

## Common Test Patterns

### Pure Function Test

```gdscript
class_name MathHelpersTest
extends GdUnitTestSuite

const __source = "res://scripts/utils/math_helpers.gd"

func test_clamp_health() -> void:
    assert_int(MathHelpers.clamp_health(150, 100)).is_equal(100)
    assert_int(MathHelpers.clamp_health(-10, 100)).is_equal(0)
    assert_int(MathHelpers.clamp_health(50, 100)).is_equal(50)
```

### Object Lifecycle Test

```gdscript
class_name PlayerStatsTest
extends GdUnitTestSuite

const __source = "res://scripts/player/player_stats.gd"

var _stats: PlayerStats

func before_test() -> void:
    _stats = auto_free(PlayerStats.new()) as PlayerStats

#region damage
func test_take_damage_reduces_health() -> void:
    _stats.take_damage(30)
    assert_int(_stats.health).is_equal(70)

func test_take_damage_does_not_go_below_zero() -> void:
    _stats.take_damage(200)
    assert_int(_stats.health).is_equal(0)
#endregion

#region healing
func test_heal_does_not_exceed_max() -> void:
    _stats.take_damage(10)
    _stats.heal(50)
    assert_int(_stats.health).is_equal(100)
#endregion
```

### Parameterized Test

```gdscript
func test_damage_calculation(
    base_damage: int,
    armor: int,
    expected: int,
    test_parameters := [
        [100, 0, 100],
        [100, 50, 50],
        [100, 100, 0],
        [100, 150, 0],
    ]
) -> void:
    var result: int = DamageCalculator.calculate(base_damage, armor)
    assert_int(result).is_equal(expected)
```

### Fuzz Test

```gdscript
func test_health_never_negative(
    fuzzer := Fuzzers.rangei(0, 10000),
    fuzzer_iterations := 200
) -> void:
    var stats := auto_free(PlayerStats.new()) as PlayerStats
    stats.take_damage(fuzzer.next_value())
    assert_int(stats.health).is_greater_equal(0)
```

---

## Scene Integration Testing

### Basic Scene Test

```gdscript
class_name PlayerSceneTest
extends GdUnitTestSuite

func test_player_moves_right_on_input() -> void:
    var runner := scene_runner("res://scenes/player.tscn")
    var start_x: float = runner.get_property("position").x

    runner.simulate_action_press("move_right")
    await runner.simulate_frames(30)
    runner.simulate_action_release("move_right")

    var end_x: float = runner.get_property("position").x
    assert_float(end_x).is_greater(start_x)
```

### Key Simulation (press vs pressed)

```gdscript
func test_jump_with_key_tap() -> void:
    var runner := scene_runner("res://scenes/player.tscn")

    # simulate_key_pressed = async tap (press + frame + release). MUST await.
    await runner.simulate_key_pressed(KEY_SPACE)
    await runner.simulate_frames(10)

    assert_bool(runner.invoke("is_jumping")).is_true()

func test_hold_shift_while_moving() -> void:
    var runner := scene_runner("res://scenes/player.tscn")

    # simulate_key_press = hold (sync). Must manually release.
    runner.simulate_key_press(KEY_SHIFT)
    runner.simulate_action_press("move_right")
    await runner.simulate_frames(30)
    runner.simulate_action_release("move_right")
    runner.simulate_key_release(KEY_SHIFT)

    assert_bool(runner.invoke("is_sprinting")).is_true()
```

### Testing with Time Factor

```gdscript
func test_enemy_patrol_completes() -> void:
    var runner := scene_runner("res://scenes/enemy.tscn")
    runner.set_time_factor(9.0)  # max 9x speed
    await runner.simulate_frames(120)
    assert_bool(runner.invoke("has_completed_patrol")).is_true()
```

### Testing Child Node State

```gdscript
func test_score_label_updates() -> void:
    var runner := scene_runner("res://scenes/hud.tscn")
    runner.invoke("add_score", 100)
    await runner.simulate_frames(1)

    var label: Label = runner.find_child("ScoreLabel")
    assert_str(label.text).is_equal("100")
```

### Spy + Scene Runner (verify method calls)

```gdscript
func test_scene_interaction() -> void:
    var spy_scene: Variant = spy("res://scenes/game.tscn")
    var runner := scene_runner(spy_scene)

    await runner.simulate_key_pressed(KEY_SPACE)
    await runner.await_input_processed()

    verify(spy_scene).on_jump()
```

### Touch Input

```gdscript
func test_touch_drag() -> void:
    var runner := scene_runner("res://scenes/puzzle.tscn")
    await runner.simulate_screen_touch_drag_drop(0, Vector2(50, 50), Vector2(200, 50))
    await runner.simulate_frames(5)
    assert_bool(runner.invoke("is_piece_moved")).is_true()
```

---

## Signal Testing Patterns

**Critical: Call `monitor_signals()` before asserting signals.**

### Basic Signal Test

```gdscript
func test_player_emits_died_signal() -> void:
    var player := monitor_signals(auto_free(Player.new()))
    player.health = 10
    player.take_damage(20)
    await assert_signal(player).is_emitted("died")
```

### Signal with Arguments

```gdscript
func test_damage_signal_carries_amount() -> void:
    var player := monitor_signals(auto_free(Player.new()))
    player.take_damage(25)
    await assert_signal(player).is_emitted("damage_taken", 25)
```

### Signal NOT Emitted

```gdscript
func test_no_death_signal_when_alive() -> void:
    var player := monitor_signals(auto_free(Player.new()))
    player.take_damage(10)
    # Use short timeout — is_not_emitted waits the full duration
    await assert_signal(player).wait_until(200).is_not_emitted("died")
```

### Signal with Scene Runner

```gdscript
func test_button_triggers_game_start() -> void:
    var runner := scene_runner("res://scenes/menu.tscn")
    var button: Button = runner.find_child("StartButton")

    runner.set_mouse_position(button.global_position + button.size / 2)
    runner.simulate_mouse_button_pressed(MOUSE_BUTTON_LEFT)

    await runner.await_signal("game_started", [], 2000)
```

---

## Error & Warning Testing

```gdscript
func test_invalid_input_pushes_error() -> void:
    await assert_error(func() -> void:
        my_system.process_invalid_data(null)
    ).is_push_error("Invalid data: null")

func test_deprecated_api_warns() -> void:
    await assert_error(func() -> void:
        my_system.old_method()
    ).is_push_warning("old_method is deprecated")

func test_valid_input_no_error() -> void:
    await assert_error(func() -> void:
        my_system.process_valid_data("hello")
    ).is_success()
```

---

## Mock Patterns

### Isolating Dependencies

```gdscript
func test_shop_charges_player() -> void:
    var wallet: Variant = mock(Wallet)
    do_return(100).on(wallet).get_gold()

    var shop := auto_free(Shop.new()) as Shop
    shop.wallet = wallet
    shop.buy_item("sword", 50)

    verify(wallet, 1).spend(50)
```

### Mock with CALL_REAL_FUNC (selective stubbing)

```gdscript
func test_partial_mock() -> void:
    var enemy: Variant = mock(Enemy, CALL_REAL_FUNC)
    # Real methods work, but stub one specific method
    do_return(999).on(enemy).get_max_health()

    assert_int(enemy.get_max_health()).is_equal(999)
    # Other methods still call real implementation
```

### Mock a Scene

```gdscript
func test_scene_mock() -> void:
    # Scene mocks always use CALL_REAL_FUNC mode
    var scene: Variant = mock("res://scenes/player.tscn")
    do_return(true).on(scene).is_invincible()
    assert_bool(scene.is_invincible()).is_true()
```

---

## Spy Patterns

### Verify Real Object Interactions

```gdscript
func test_inventory_adds_item() -> void:
    var inventory := auto_free(Inventory.new())
    var spy_inv: Variant = spy(inventory)

    spy_inv.add_item("sword")

    verify(spy_inv, 1).add_item("sword")
    # Real behavior preserved — item is actually in inventory
    assert_array(spy_inv.get_items()).contains("sword")
```

### Spy on Scene for Integration Verification

```gdscript
func test_game_calls_save_on_checkpoint() -> void:
    var spy_scene: Variant = spy("res://scenes/game.tscn")
    var runner := scene_runner(spy_scene)

    runner.invoke("reach_checkpoint")
    await runner.simulate_frames(5)

    verify(spy_scene, 1).save_progress()
```

---

## Testing Resources

```gdscript
class_name WeaponDataTest
extends GdUnitTestSuite

func test_load_weapon_resource() -> void:
    var weapon := load("res://resources/weapons/sword.tres") as WeaponData
    assert_object(weapon).is_not_null()
    assert_str(weapon.name).is_equal("Sword")
    assert_int(weapon.damage).is_greater(0)

func test_weapon_dps_calculation() -> void:
    var weapon := auto_free(WeaponData.new()) as WeaponData
    weapon.damage = 10
    weapon.attack_speed = 2.0
    assert_float(weapon.get_dps()).is_equal_approx(20.0, 0.01)
```

---

## Testing State Machines

```gdscript
class_name PlayerStateMachineTest
extends GdUnitTestSuite

func test_starts_in_idle_state() -> void:
    var runner := scene_runner("res://scenes/player.tscn")
    assert_str(runner.get_property("current_state")).is_equal("idle")

func test_transitions_to_run_on_input() -> void:
    var runner := scene_runner("res://scenes/player.tscn")
    runner.simulate_action_press("move_right")
    await runner.simulate_frames(2)
    assert_str(runner.get_property("current_state")).is_equal("run")
```

---

## Memory Management Best Practices

### Scope Matching

```gdscript
var _shared_node: Node2D

func before() -> void:
    # Suite-level allocation → free in after()
    _shared_node = Node2D.new()

func after() -> void:
    _shared_node.free()

func before_test() -> void:
    # Per-test allocation → auto_free handles it
    var _test_node := auto_free(Node.new())
```

### What NOT to auto_free

- Scene runner scenes loaded by path (already auto-freed internally)
- Mock and spy instances (framework handles them)
- But DO auto_free the source instance for a spy: `spy(auto_free(Node.new()))`

### Temp Files

```gdscript
func test_file_operations() -> void:
    var dir := create_temp_dir("my_test")
    var file := create_temp_file("my_test", "data.txt")
    # Auto-cleaned after suite's after() hook
```

---

## CI Setup

### GitHub Action

```yaml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: godot-gdunit-labs/gdUnit4-action@v1
        with:
          godot-version: '4.6'
          godot-status: 'stable'
          version: 'installed'          # use the version in your project
          paths: |
            res://test/
          timeout: 10                   # minutes
          report-name: 'test-results'

      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: test-reports
          path: reports/
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0` | All tests passed |
| `100` | Test failures or errors |
| `101` | Tests passed but orphan nodes detected |
| `103` | Headless mode not supported |
| `104` | Godot version not supported |
| `105` | Script compilation errors detected |

### Reports

GdUnit4 generates HTML reports (visual) and JUnit XML (`results.xml`) for CI integration. Reports saved to `res://reports/report_N/` with automatic rotation.

---

## CLI Usage

```bash
# Set the Godot binary
export GODOT_BIN=/path/to/godot

# Run all tests in a directory
./addons/gdUnit4/runtest.sh -a res://test/

# Run a specific test suite
./addons/gdUnit4/runtest.sh -a res://test/unit/PlayerTest.gd

# Ignore specific tests
./addons/gdUnit4/runtest.sh -a res://test/ -i PlayerTest -i EnemyTest:test_specific_case

# Continue on failure (disable fail-fast)
./addons/gdUnit4/runtest.sh -a res://test/ -c

# Custom report directory
./addons/gdUnit4/runtest.sh -a res://test/ -rd res://my_reports/

# Specify Godot binary directly
./addons/gdUnit4/runtest.sh --godot_binary /path/to/godot -a res://test/
```

### CLI Options

| Short | Long | Description |
|-------|------|-------------|
| `-a` | `--add` | Add test suite or directory (repeatable) |
| `-i` | `--ignore` | Ignore test suite or test case (repeatable) |
| `-c` | `--continue` | Disable fail-fast |
| `-conf` | `--config` | Load test configuration file |
| `-rd` | `--report-directory` | Report output directory |
| `-rc` | `--report-count` | Report history files to keep (default: 20) |
| | `--ignoreHeadlessMode` | Allow headless mode (input won't work) |

---

## Hidden Traps & Gotchas

### Async Traps (the #1 source of silent bugs)

1. **Forgetting `await` on signal assertions.** Without `await`, `assert_signal().is_emitted()` always passes silently.
2. **Forgetting `await` on `assert_error()`.** The callable may never execute.
3. **Forgetting `await` on `simulate_key_pressed()`.** The key release never happens, leaving it "stuck."
4. **Forgetting `await` on `assert_func()`.** Returns immediately without polling.

### Mock & Spy Traps

5. **Mocking an instance.** `mock(Node.new())` fails. Use `mock(Node)`.
6. **Spying on a class.** `spy(Node)` returns null. Use `spy(Node.new())`.
7. **Stubbing void functions.** `do_return("x").on(m).void_method()` errors.
8. **`do_nothing()` does not exist.** RETURN_DEFAULTS mode already returns defaults.
9. **Mocking singletons.** `mock(Input)` — not allowed.
10. **Scene mocks without scripts.** `.tscn` without a script cannot be mocked.

### Naming & Discovery Traps

11. **Test functions must start with `test_`.** Other names are ignored.
12. **Lifecycle hooks have exact names.** `before()`, `after()`, `before_test()`, `after_test()` — not `setup()`, `teardown()`.
13. **Unknown test arguments cause silent skips.** Typos in `do_skip`, `skip_reason`, `test_parameters`, `fuzzer_iterations`, `fuzzer_seed`, `timeout` → test silently skipped.

### Memory Traps

14. **Orphans in `before()` vs `before_test()`.** Match scope: `before()`→`after()`, `before_test()`→`after_test()`.
15. **Scene runner auto-frees scenes loaded by path.** Don't manually free them.
16. **Project settings are snapshot/restored per suite.** Changes persist within a suite but reset after.

### Input Simulation Traps

17. **Headless mode breaks input.** CLI exits with code 103. Use `--ignoreHeadlessMode` only if you don't need input tests.
18. **Modifier keys carry forward within a test.** Always release keys you press.
19. **`set_time_factor()` modifies global `Engine.time_scale`.** Reset on runner destruction, but crashes can leave it modified.
20. **`simulate_key_press` vs `simulate_key_pressed`.** `press` = hold (sync). `pressed` = tap (async, must await).

### Configuration Traps

21. **`push_error()` doesn't fail tests by default.** `report/godot/push_error` defaults to `false`.
22. **Strict number type comparison is on.** `assert_int(1).is_equal(1.0)` fails.
23. **Test timeout is 5 minutes by default.** Hanging tests block CI for 5 min.
24. **Test discovery is off by default.** New files aren't auto-detected.

### CI Traps

25. **Exit code 101 = orphans, not failure.** Configure CI to accept 101 or fix orphans.
26. **The `--remote-debug tcp://127.0.0.1:0` flag is critical.** Without it, Godot's debugger can hang CI forever.
27. **Linux CI needs a display server.** Use `xvfb` or the GitHub Action (handles it automatically).

### Assertion Traps

28. **`assert_that()` has limited methods.** Only `is_null`, `is_not_null`, `is_equal`, `is_not_equal`.
29. **Array assertion variadic args.** `.contains(1, 2, 3)` = individual elements. `.contains([1, 2, 3])` = one array element.
30. **`is_same()` vs `is_equal()`.** `is_same()` = reference identity. `is_equal()` = value equality.
31. **`monitor_signals()` required before signal assertions.** Without it, signals aren't captured.

---

## Performance Tips

- Use `before()` for expensive one-time setup, `before_test()` for cheap per-test setup.
- Use `set_time_factor()` (max 9.0) to speed up scene runner tests with animations/timers.
- Set `fuzzer_iterations` judiciously — 1000 (default) may be overkill.
- Use `-c` (continue) in CI to get full failure picture instead of stopping at first.
- Set shorter per-test timeouts: `func test_quick(timeout := 2000)`.
- Disable `verbose_orphans` in CI if orphan tracking is too noisy.
