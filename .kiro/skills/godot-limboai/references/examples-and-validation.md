# Examples and Validation

## Example 1: Simple Patrol Enemy (Pure BT)

**Request**: "Make the slime enemy patrol between random positions and chase the player when nearby."

**Expected output**:
- Custom `SelectRandomNearbyPos` BTAction in `res://ai/tasks/actions/`
- Custom `MoveToPosition` BTAction in `res://ai/tasks/actions/`
- BehaviorTree `.tres` using BTSelector → chase sequence + patrol sequence
- BTPlayer node added to slime scene
- BlackboardPlan with `target`, `patrol_pos`, `speed` variables

**Validation**:
- All custom tasks have `@tool` and `_generate_name()`
- BB variable properties use `_var` suffix
- `is_instance_valid()` guards on target access
- Built-in `BTCheckVar`, `BTWait` used where applicable
- `get_diagnostics` passes on all `.gd` files

## Example 2: Boss with Phase Transitions (BT + HSM Hybrid)

**Request**: "Create a boss that has melee phase, ranged phase at 50% HP, and enrage at 20% HP."

**Expected output**:
- LimboHSM with three BTState children
- Three BehaviorTree `.tres` resources (one per phase)
- Health-change signal dispatching HSM events
- Custom tasks for phase-specific behaviors
- Variable mapping between HSM and BT scopes

**Validation**:
- HSM transitions use named events, not direct state references
- BTState has `success_event` and `failure_event` configured
- Variable mapping is explicit in BTState inspector, not relying on parent scope
- Phase transitions are one-directional (no going back to phase 1)

## Example 3: Squad Coordination (Shared Blackboard)

**Request**: "Make a group of enemies where only 2 can attack the player at once."

**Expected output**:
- Squad manager script setting shared parent blackboard
- Custom `ClaimAttackSlot` BTCondition
- Custom `ReleaseAttackSlot` cleanup in `_exit()`
- `active_attackers` variable in shared blackboard

**Validation**:
- Shared blackboard accessed via `blackboard.top()`
- Attack slot released in `_exit()` to prevent leaks
- Slot count clamped with `maxi(0, ...)` for safety
- No direct parent scope access from BT tasks

## Common Validation Checklist

After any LimboAI implementation, verify:

1. **Editor integration**: All tasks with `_generate_name()` have `@tool`
2. **Blackboard safety**: Object vars guarded with `is_instance_valid()`, no type hints on nullable objects
3. **Naming**: BB variable properties end with `_var`, `LimboUtility.decorate_var()` used in display names
4. **Performance**: No expensive ops in `_tick()`, `distance_squared_to()` preferred
5. **Built-in preference**: No custom task duplicates built-in functionality (check `references/builtin-tasks.md`)
6. **File organization**: Tasks in `res://ai/tasks/`, trees in `res://ai/trees/` (or project convention)
7. **Script diagnostics**: `get_diagnostics` clean on all changed files (editor-connected mode)
