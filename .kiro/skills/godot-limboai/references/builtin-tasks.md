# Built-in Tasks Reference

## Scene Tasks

### BTPlayAnimation
Plays an animation on an AnimationPlayer.
- `animation_player: BBNode` — reference to AnimationPlayer
- `animation_name: StringName` — animation to play
- `await_completion: float` — seconds to wait (0 = don't wait)
- `blend: float` — blend time (-1 = default)
- `speed: float` — playback speed
- `from_end: bool` — play from end
- Returns RUNNING while waiting, SUCCESS when done

### BTAwaitAnimation
Waits for an animation to finish.
- `animation_player: BBNode`
- `animation_name: StringName` — if set, only waits for this specific animation
- Returns RUNNING while playing, SUCCESS when finished

### BTStopAnimation
Stops animation playback.
- `animation_player: BBNode`
- `keep_state: bool` — if true, keeps current animation frame
- Returns SUCCESS

### BTPauseAnimation
Pauses/unpauses animation.
- `animation_player: BBNode`
- `pause: bool`
- Returns SUCCESS

### BTSetAgentProperty
Sets a property on the agent node.
- `property: NodePath` — property path on agent
- `value: BBVariant` — value to set (can be BB variable)
- `operation: LimboUtility.Operation` — NONE, ADD, SUBTRACT, MULTIPLY, DIVIDE
- Returns SUCCESS, FAILURE if property not found

### BTCheckAgentProperty
Checks an agent property against a value.
- `property: NodePath`
- `value: BBVariant`
- `check_type: LimboUtility.CheckType` — EQUAL, LESS_THAN, GREATER_THAN, etc.
- Returns SUCCESS if check passes, FAILURE otherwise

## Utility Tasks

### BTWait
Waits for a specified duration.
- `duration: float` — seconds to wait
- Returns RUNNING until duration elapsed, then SUCCESS

### BTWaitTicks
Waits for a specified number of ticks.
- `num_ticks: int`
- Returns RUNNING until ticks elapsed, then SUCCESS

### BTRandomWait
Waits for a random duration within range.
- `min_duration: float`
- `max_duration: float`
- Returns RUNNING until elapsed, then SUCCESS

### BTCallMethod
Calls a method on the agent or another node.
- `method: StringName`
- `node: BBNode` — target node (defaults to agent)
- `args: Array` — method arguments
- `include_delta: bool` — pass delta as first argument
- Returns SUCCESS if method exists, FAILURE otherwise

### BTConsolePrint
Prints a message (useful for debugging).
- `text: String` — message with BB variable substitution via `{var_name}`
- Returns SUCCESS

### BTEvaluateExpression
Evaluates a Godot Expression.
- `expression: String`
- `result_var: StringName` — BB variable to store result
- Returns SUCCESS/FAILURE

### BTFail
Always returns FAILURE. Useful as a placeholder or to force failure in a branch.

## Blackboard Tasks

### BTSetVar
Sets a blackboard variable.
- `variable: StringName` — variable to set
- `value: BBVariant` — value (can reference another BB variable)
- `operation: LimboUtility.Operation` — NONE, ADD, SUBTRACT, MULTIPLY, DIVIDE
- Returns SUCCESS

### BTCheckVar
Checks a blackboard variable against a value.
- `variable: StringName`
- `value: BBVariant`
- `check_type: LimboUtility.CheckType`
- Returns SUCCESS if check passes, FAILURE otherwise

### BTCheckTrigger
Checks if a trigger variable is set, then resets it.
- `variable: StringName` — trigger variable (expected bool)
- Returns SUCCESS if trigger was true (and resets it), FAILURE otherwise

## Composites

### BTSequence
Executes children left-to-right. Fails on first FAILURE. Succeeds when all succeed.

### BTSelector
Executes children left-to-right. Succeeds on first SUCCESS. Fails when all fail.

### BTParallel
Executes all children each tick.
- `num_successes_required: int` — how many must succeed (default: 1)
- `num_failures_required: int` — how many must fail to return FAILURE (default: 1)
- `repeat: bool` — restart completed children

### BTDynamicSequence / BTDynamicSelector
Like Sequence/Selector but re-evaluates from the first child every tick. If a higher-priority child changes status, lower children are aborted.

### BTRandomSequence / BTRandomSelector
Like Sequence/Selector but children are shuffled each time the composite is entered.

### BTProbabilitySelector
Selects one child based on weighted probability.
- Each child has a `weight: float` — relative probability
- `abort_on_failure: bool` — if true, returns FAILURE immediately when selected child fails; if false, tries another

## Decorators

### BTRepeat
- `times: int` — number of repetitions
- `forever: bool` — ignore times, repeat indefinitely
- `abort_on_failure: bool` — stop repeating on child FAILURE

### BTRepeatUntilSuccess / BTRepeatUntilFailure
Repeats child until it returns the target status.

### BTInvert
Flips SUCCESS ↔ FAILURE. RUNNING passes through.

### BTAlwaysSucceed / BTAlwaysFail
Overrides child result (RUNNING still passes through).

### BTCooldown
- `duration: float` — cooldown in seconds
- `process_pause: bool` — count time while paused
- `start_cooled: bool` — start in cooldown state
- `trigger_on_failure: bool` — trigger cooldown even on child FAILURE

### BTDelay
- `seconds: float` — delay before running child
- Returns RUNNING during delay

### BTTimeLimit
- `time_limit: float` — max seconds for child
- Aborts child and returns FAILURE if time exceeded

### BTRunLimit
- `run_limit: int` — max number of runs
- `count_policy` — COUNT_SUCCESSFUL, COUNT_FAILED, or COUNT_ALL

### BTForEach
- `array_var: StringName` — BB variable holding an Array
- `element_var: StringName` — BB variable to store current element
- `index_var: StringName` — BB variable to store current index (optional)

### BTProbability
- `run_chance: float` — probability (0.0–1.0) of running child
- Returns FAILURE if not selected

### BTSubtree
- `subtree: BehaviorTree` — external BT resource to execute
- Creates a new blackboard scope automatically

### BTNewScope
Creates a new blackboard scope for its child. The child's blackboard has the current blackboard as parent. Useful for isolating variable writes.
