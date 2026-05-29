# Decision Lenses

Use these lenses to choose a Godot-friendly architecture.

## Lens 0: Project Fit

Prefer:

- extending sound existing project patterns over replacing them casually
- preserving current input, platform, scene, and content assumptions unless change is explicit
- architecture that can actually be adopted by the current repo without broad collateral rewrites

Question:

- what already exists that this design must remain compatible with?

## Lens 0.5: Available Capabilities

Before designing a subsystem, check what the project's addons already provide. Scan `addons/` and `project.godot` for active plugins.

| Domain | Addon | Autoload / Entry Point | Specialist Skill |
|--------|-------|-----------------------|-----------------|
| AI / Behavior | LimboAI | BTPlayer nodes | `godot-limboai` |
| Dialogue / Narrative | Dialogue Manager | DialogueManager autoload | `godot-dialogue` |
| Audio | Sound Manager | SoundManager autoload | `godot-sound` |
| Game Feel / Juice | GodotFeel | FeedbackPlayer nodes | `godot-feel` |
| UI Navigation | CommonUI | UIRouter, UIInputRouter, TransitionManager autoloads | `godot-common-ui` |
| Camera | camera_system | CameraDirector2D/3D nodes | `godot-camera` |
| Logging | GLog | GLog autoload | `godot-logger` |
| Procedural UI Shapes | procedural_shader_utility | ProceduralPanel2D nodes | `godot-procedural-ui` |
| Testing | gdUnit4 | GdUnitTestSuite base class | `godot-gdunit4` |

Do not design a custom solution for a domain an active addon already covers. Note the addon as an architectural constraint and defer implementation details to the specialist skill.

If an addon is present but the architect is unsure whether it fits the current need, state the uncertainty and recommend the specialist skill evaluate it.

Question:

- which addons are active, and does any of them already solve part of this feature?

## Lens 1: Scene Topology

Prefer:

- composition over scene inheritance
- flat enough trees for maintainability
- topology based on spatial/render needs, not arbitrary ownership

When an entity has a variable set of behaviors (items with optional capabilities, monsters with mix-and-match abilities, spells with interchangeable effects), model each behavior as a separate composable object rather than using inheritance or flag fields. In Godot: use child nodes when the capability needs lifecycle or signals, Resources when it's pure data/config, or Callables when it's a single function. This keeps entity classes small and lets content be defined in data files or the inspector.

Question:

- does this feature need reusable components, or a brittle inherited hierarchy?

## Lens 2: State Placement

Choose between:

- node properties for mutable per-instance runtime state
- `Resource` for shared config or serialized data
- registry/service object when cross-system lookup would otherwise become path-coupled

If mutable `Resource` state is involved, consult `godot-scene-resource`.

## Lens 2.5: Asset Availability

When choosing visual/spatial approaches, check whether the required assets actually exist in the project:

- **If real texture assets exist**: TileMapLayer, AtlasTexture, SpriteFrames, and other asset-dependent nodes are viable.
- **If only placeholder/prototype art is available (or none at all)**: prefer simpler approaches that don't require authored assets. Use ColorRect backgrounds with collision arrays instead of TileMapLayer. Use primitive shapes and `_draw()` instead of sprite-based rendering. Use code-driven layouts instead of atlas-dependent tilemaps.

Don't recommend TileMapLayer with custom data layers when there are no TileSet texture assets. Creating a functional TileSet with atlas sources requires real textures or extensive `.tres` boilerplate that adds friction without gameplay value in a prototype.

## Lens 3: Language and Runtime

Default to GDScript when:

- the work is scene-tree heavy
- iteration speed matters
- the task is normal gameplay logic

Do not recommend C#, GDExtension, or server-level optimization without a specific bottleneck or platform reason.

## Lens 4: Dependency Strategy

Prefer:

- direct references injected at setup time
- scene-unique or explicit ownership when local
- shared resources or registries instead of fragile path traversal

Avoid defaulting to global singletons unless the state is truly global.

## Lens 5: Concurrency

Keep scene-tree mutation on the main thread.

Use background or worker-thread work only for:

- pure data processing
- background loading
- isolated calculations whose outputs can be applied later on the main thread

## Lens 6: Performance

Use measured behavior, not folklore.

Defaults:

- idle node count alone is usually not the first problem — unless entity count exceeds ~1000, at which point per-Node overhead (~12-14μs/node/frame on M-series) dominates
- instantiation spikes matter more than static existence
- signals are fine until proven otherwise

Mass-entity thresholds (Godot 4.6, Apple M-series, measured):

- **< 500 entities**: Node-based is fine. No optimization needed.
- **500-1000 entities**: Node overhead becomes visible. Consider manager pattern with pooling.
- **1000-12000 entities**: Node-free architecture required. PackedArray state + RenderingServer canvas items + C++ compute. GDScript iteration + 1 RS call/entity ceiling: ~12k at 60 FPS.
- **12000-22000 entities**: Requires combined C++ call (pack + steer + RS transforms in one GDExtension method). Eliminates GDScript pack loop and multiple bridge crossings. Measured ceiling: ~22k at 60 FPS.
- **22000-30000 entities**: Add frustum-culled separation (skip spatial queries for off-screen entities) + alternate-frame parity (run separation at 30Hz per entity). Measured ceiling: ~30k at 60 FPS.
- **30000+ entities**: Remaining bottleneck is direction compute (atan2, normalize, multiply for ALL entities) + GDScript extras loop. Would need SIMD, MultiMesh rendering, or moving extras to C++.

Bridge-crossing rule: each GDScript→Engine API call costs ~0.25μs fixed overhead. At N entities, the optimization target is reducing per-entity call count, not making each call faster. Two RS calls/entity vs one = 0.25μs × N difference per frame.

Composite draw rule: for mass entities using RenderingServer, draw all visual layers (shadow, shape, outline) into one canvas item. One `set_transform` call moves everything. Never use separate canvas items for visual layers that always move together.

## Lens 7: Validation Surface

Prefer approaches that the current toolchain can validate honestly.

Avoid plans that depend on unsupported editor/runtime capabilities or hidden manual steps.

## Output Template

```md
# Architecture Decision

## Problem
- <what must be built>

## Chosen Approach
- <pattern and why>

## Compatibility Constraints
- <what current project assumptions this preserves>

## Rejected Alternatives
- <alternative> -> <why rejected>

## Risks
- <risk> -> <mitigation>

## Follow-On Skills
- godot-orchestrator
- <other skill if needed>
```

### Extended Sections (for complex features only)

Include these when the feature has multiple interacting components, non-trivial algorithms, or backward compatibility constraints. Skip them for simple features.

```md
## Component Interfaces

### <ComponentName> (<file path>)
<one-line purpose>

Signals:
- signal <name>(<params>)

Public API:
- func <name>(<params>) -> <return>

Exports:
- @export var <name>: <type>

### <ComponentName2> (<file path>)
...
```

```md
## Key Algorithm Pseudocode

### <Algorithm Name>
<GDScript-style pseudocode for the non-trivial logic>
<Include pre/postconditions when the algorithm has subtle correctness requirements>
```

```md
## Error Classification

| Scenario | Category | Response |
|----------|----------|----------|
| <description> | A (crash) | assert() or push_error() + return |
| <description> | B (visible degradation) | push_error() + placeholder/fallback |
| <description> | C (handle) | <specific recovery logic> |
```

```md
## Correctness Properties

### Property 1: <name>
For any <condition>, <invariant> shall hold.
Validates: <what this protects against>

### Property 2: <name>
...
```

## Escalate

Consult another specialist skill when:

- mutable resources or scene instancing are involved -> `godot-scene-resource`
- persistence or restoration is involved -> `godot-persistence`
- the scoped slice is still too broad -> `godot-scope`
- AI behavior trees or HSMs are involved -> `godot-limboai`
- dialogue or branching narrative is involved -> `godot-dialogue`
- audio playback, music, or SFX is involved -> `godot-sound`
- game feel, juice, or feedback sequences are involved -> `godot-feel`
- UI navigation, screen flow, or transitions are involved -> `godot-common-ui`
- camera rigs, follow, or shake are involved -> `godot-camera`
- structured logging is involved -> `godot-logger`
- procedural UI shapes are involved -> `godot-procedural-ui`
- unit or integration testing is involved -> `godot-gdunit4`
