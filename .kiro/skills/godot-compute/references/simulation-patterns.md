# Simulation Patterns

Reference for boids/flocking and SPH fluid simulation (2D and 3D) on GPU compute in Godot 4.

## Table of Contents

1. [Boids / Flocking](#boids)
2. [SPH Fluid — 3D](#sph-3d)
3. [SPH Fluid — 2D](#sph-2d)
4. [SPH Pass Graph](#sph-pass-graph)
5. [Integration and Boundaries](#integration)

---

## 1. Boids / Flocking <a name="boids"></a>

### Core Forces

Three local steering urges per boid `i`, evaluated over neighbors `N_i` within perception radius `r`:

**Separation** — steer away from nearby boids, weighted by inverse distance:

```
s_i = Σ_{j ∈ N_i(r_s)} (x_i - x_j) / (|x_i - x_j|² + ε)
```

**Alignment** — steer toward local average velocity:

```
a_i = (1/|N_i(r_a)|) Σ_{j ∈ N_i(r_a)} v_j  −  v_i
```

**Cohesion** — steer toward local centroid:

```
c_i = (1/|N_i(r_c)|) Σ_{j ∈ N_i(r_c)} x_j  −  x_i
```

### Desired-Velocity Steering

Convert raw vectors to clamped steering requests:

```
steer(d, v_i) = clamp_len(normalize(d) * v_max − v_i, f_max)
```

Combined force:

```
f_i = w_s * steer(s_i) + w_a * steer(a_i) + w_c * steer(c_i)
```

Clamp each term individually before weighting, then clamp the final sum. This prevents a single close-neighbor separation spike from overpowering alignment and cohesion.

### Parameter Hierarchy

Use `r_s < r_a ≤ r_c` with separation carrying highest effective urgency. Set the **grid cell edge = max(r_s, r_a, r_c)** so neighbor queries stay within a fixed 3×3 (2D) or 3×3×3 (3D) cell stencil. Apply per-force radii in-shader.

### Obstacle Avoidance

**SDF steering** for static geometry: sample a 3D signed distance field at a predicted future position. If the SDF value is near-zero or negative, steer away proportional to penetration depth:

```glsl
vec3 future_pos = pos + vel * look_ahead_time;
float sdf_val = texture(sdf_texture, world_to_sdf(future_pos)).r;
if (sdf_val < avoidance_margin) {
    vec3 grad = sdf_gradient(future_pos);  // central differences
    vec3 avoid = normalize(grad) * (avoidance_margin - sdf_val) * avoidance_strength;
    force += steer(avoid, vel);
}
```

For dynamic obstacles, fall back to predictive sphere/capsule tests.

### Goal Seeking with Arrival

Distance-scaled desired speed for smooth arrival:

```
d = |x_goal − x_i|
v_desired = normalize(x_goal − x_i) * v_max * min(d / arrival_radius, 1.0)
f_goal = clamp_len(v_desired − v_i, f_max_goal)
```

### Boids Pass Graph

```
clear grid → compute hashes → sort → find cell starts → reorder state
→ boids force accumulation (separation + alignment + cohesion + obstacles + goals)
→ integration → write to MultiMesh buffer
```

All passes use the sorted-grid infrastructure from `spatial-data-structures.md`. The force pass replaces SPH density/pressure with the three boid forces.

### Boids Buffer Layout

| Buffer | Contents |
|--------|----------|
| `boid_state_read` | vec4 pos, vec4 vel per boid (ping-pong read) |
| `boid_state_write` | vec4 pos, vec4 vel per boid (ping-pong write) |
| `params` | UBO: dt, weights, radii, v_max, f_max, goal_pos, grid dims |
| Grid buffers | Same as spatial-data-structures.md |
| `multimesh_buffer` | Output: transform data written directly to MultiMesh RD RID |

## 2. SPH Fluid — 3D <a name="sph-3d"></a>

### Smoothing Kernels (3D)

**Poly6** — for density:

```
W_poly6(r, h) = (315 / 64πh⁹) * (h² − r²)³     for 0 ≤ r ≤ h
```

**Spiky gradient** — for pressure force (non-zero gradient at r→0):

```
∇W_spiky(r, h) = −(45 / πh⁶) * (h − r)² * (r_vec / r)     for 0 ≤ r ≤ h
```

**Viscosity Laplacian** — positive everywhere, prevents clumping:

```
∇²W_visc(r, h) = (45 / πh⁶) * (h − r)     for 0 ≤ r ≤ h
```

### Density and Pressure

```
ρ_i = Σ_j m_j * W_poly6(|x_i − x_j|, h)
p_i = k * (ρ_i − ρ_0)
```

Where `k` is stiffness and `ρ_0` is rest density.

### Forces

**Pressure** (symmetrized):

```
f_pressure_i = −Σ_j m_j * (p_i + p_j) / (2 * ρ_j) * ∇W_spiky(x_i − x_j, h)
```

**Viscosity** (symmetrized):

```
f_visc_i = μ * Σ_j m_j * (v_j − v_i) / ρ_j * ∇²W_visc(|x_i − x_j|, h)
```

**Total acceleration:**

```
a_i = f_i / ρ_i + g
```

Where `g` is gravity.

### Integration

Leapfrog (second-order, one force evaluation per step):

```
v_i(t+dt) = v_i(t) + a_i * dt
x_i(t+dt) = x_i(t) + v_i(t+dt) * dt
```

Or semi-implicit Euler for simplicity.

## 3. SPH Fluid — 2D <a name="sph-2d"></a>

Same algorithmic structure as 3D, but with **different normalization constants**. The 3D constants are wrong for 2D — using them produces incorrect density and force magnitudes.

### Smoothing Kernels (2D)

**Poly6 (2D):**

```
W_poly6(r, h) = (4 / πh⁸) * (h² − r²)³     for 0 ≤ r ≤ h
```

**Spiky gradient (2D):**

```
∇W_spiky(r, h) = −(30 / πh⁵) * (h − r)² * (r_vec / r)     for 0 ≤ r ≤ h
```

**Viscosity Laplacian (2D):**

```
∇²W_visc(r, h) = (40 / πh⁵) * (h − r)     for 0 ≤ r ≤ h
```

### 2D Grid

Neighbor search uses 3×3 = 9 cells instead of 27. Cell hash is 2D:

```glsl
uint hash = uint(cell.x) + uint(cell.y) * grid_width;
```

All other SPH logic (density, pressure, forces, integration) is identical to 3D with the 2D kernel constants.

## 4. SPH Pass Graph <a name="sph-pass-graph"></a>

```
clear grid
→ compute hashes
→ sort
→ find cell starts
→ reorder state into sorted buffers
→ barrier
→ density + pressure pass (read sorted positions, write density[i], pressure[i])
→ barrier
→ force accumulation pass (read sorted pos/vel + density/pressure, write force[i])
→ barrier
→ integration pass (update vel[i], pos[i], apply boundaries)
→ write to render output (MultiMesh buffer or Texture2DRD)
```

Each pass is a separate compute pipeline bound over the same storage buffers. Use `compute_list_add_barrier()` between dependent passes.

### SPH Buffer Layout

| Buffer | Contents |
|--------|----------|
| `positions` | vec4 per particle (unsorted) |
| `velocities` | vec4 per particle (unsorted) |
| `sorted_positions` | vec4 per particle (cell-contiguous) |
| `sorted_velocities` | vec4 per particle (cell-contiguous) |
| `density` | float per particle |
| `pressure` | float per particle |
| `force` | vec4 per particle |
| `params` | UBO: dt, h, k, ρ_0, μ, gravity, grid dims, num_particles |
| Grid buffers | cell_keys, particle_ids, cell_start, cell_end |

### Push Constants for SPH

Typical per-dispatch constants (fits in 128 bytes):

```glsl
layout(push_constant) uniform Params {
    float dt;
    float h;           // support radius
    float k;           // stiffness
    float rest_density;
    float viscosity;
    float gravity_y;
    uint  num_particles;
    float cell_size;
    vec3  domain_min;
    uint  grid_width;
    uint  grid_height;
    uint  grid_depth;
};
```

## 5. Integration and Boundaries <a name="integration"></a>

### Boundary Handling

Simple box containment with velocity reflection:

```glsl
if (pos.x < domain_min.x) { pos.x = domain_min.x; vel.x *= -damping; }
if (pos.x > domain_max.x) { pos.x = domain_max.x; vel.x *= -damping; }
// repeat for y, z
```

For more complex boundaries, sample an SDF and push particles out along the gradient.

### Stability Guards

- Clamp maximum velocity: `vel = normalize(vel) * min(length(vel), v_max)`
- Clamp maximum acceleration: prevents explosive forces from very close particles
- Use `ε` in distance calculations to avoid division by zero: `dist = max(length(r), 1e-6)`
- Keep `dt` fixed or bounded — adaptive timestep is possible but adds complexity
- For SPH: ensure `k * dt² / m` is small enough for stability (CFL-like condition)
