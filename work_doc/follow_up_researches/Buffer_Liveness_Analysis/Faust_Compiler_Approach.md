# Static Memory Planning, Buffer Allocation, and Memory Footprint Minimization in the Faust Compiler

## Executive Summary

The Faust compiler implements a multi-layered memory optimization strategy that transforms a high-level functional DSP specification into memory-efficient C++ code. Rather than using a single "memory planner" pass, Faust achieves memory minimization through the interplay of several subsystems: **signal sharing analysis**, **occurrence-driven caching**, **DAG-based loop scheduling and fusion**, **delay line strategy selection**, **zone-based memory consolidation**, and **struct layout optimization**. These work together to minimize both the total memory footprint and the runtime memory access patterns of generated DSP code.

---

## 1. The Compilation Pipeline (Memory-Relevant Stages)

The Faust compiler transforms signals through these memory-relevant stages:

```
Faust DSP source
    → Signal normalization (simplifyToNormalForm)
    → Sharing analysis (sharingAnnotation)
    → Occurrence markup (OccMarkup::mark)
    → Code generation (DAGInstructionsCompiler / InstructionsCompiler)
        → Loop DAG construction
        → Cache/buffer decisions per signal node
        → Delay line strategy selection
    → FIR (Faust Intermediate Representation)
    → FIR-to-FIR optimization passes (processFIR)
        → Subcontainer inlining
        → Zone array consolidation (iZone/fZone)
        → Control array packing (iControl/fControl)
        → Memory layout computation
    → Backend code emission (C++, C, LLVM, Rust, WASM, etc.)
```

---

## 2. Sharing Analysis: The Foundation of Buffer Minimization

**File:** `compiler/signals/sharing.cpp`

The first critical memory optimization is **sharing analysis**, which determines which sub-expressions in the signal DAG are used multiple times. This directly controls whether an intermediate value needs a named buffer or can be computed inline.

```cpp
static void sharingAnnotation(int vctxt, Tree sig, Tree key)
{
    int count = getSharingCount(sig, key);
    if (count > 0) {
        // Not first visit — increment sharing count
        setSharingCount(sig, key, count + 1);
    } else {
        int v = getCertifiedSigType(sig)->variability();
        // "Time sharing": slower expression in faster context
        if (v < vctxt) {
            setSharingCount(sig, key, 2);  // Forces materialization
        } else {
            setSharingCount(sig, key, 1);  // Regular occurrence
        }
        // Recurse into sub-signals
        ...
    }
}
```

**Key insight:** A signal with sharing count = 1 can be inlined (no buffer needed). A signal with sharing count > 1 must be materialized into a variable or array. The "time sharing" case (a slow expression used in a fast context) is forced to sharing count 2, ensuring it gets stored rather than recomputed at the higher rate.

---

## 3. Occurrence Analysis: Delay-Aware Buffer Decisions

**Files:** `compiler/generator/occurrences.hh`, `occurrences.cpp`

The `OccMarkup` system extends sharing analysis with delay-awareness. For each signal node, it tracks:

| Field | Purpose |
|-------|---------|
| `fXVariability` | Extended variability (kKonst=0, kBlock=1, kSamp=2, kSamp+recursive=3) |
| `fOccurrences[4]` | Occurrence count per variability context |
| `fMultiOcc` | True if multiple occurrences or occurrence in higher context |
| `fMaxDelay` | Maximum delay applied to this signal |
| `fOutDelayOcc` | True if signal is used outside any delay |

The `xVariability` function combines variability with recursiveness:

```cpp
static int xVariability(int v, int r) {
    if (r > 1) r = 1;
    return std::min<int>(3, v + r);
}
```

**Memory impact:** `fMaxDelay` directly determines the size of the delay line buffer allocated for a signal. `fMultiOcc` determines whether a signal needs a named variable at all.

---

## 4. Cache Code Generation: The Core Buffer Allocation Decision

**File:** `compiler/generator/instructions_compiler.cpp` (function `generateCacheCode`)

This is where the compiler makes the critical decision about **what kind of storage** each signal node gets:

```cpp
ValueInst* InstructionsCompiler::generateCacheCode(Tree sig, ValueInst* exp)
{
    int          sharing = getSharingCount(sig, fSharingKey);
    Occurrences* o       = fOccMarkup->retrieve(sig);

    if (o->getMaxDelay() > 0) {
        // Signal is used with delay → needs a delay line buffer
        // Size determined by o->getMaxDelay()
        return generateDelayVec(sig, exp, ctype, vname, o->getMaxDelay());

    } else if (sharing > 1 || o->hasMultiOccurrences()) {
        // Shared or multi-occurrence → needs a named variable
        return generateVariableStore(sig, exp);

    } else if (sharing == 1) {
        // Single use, no delay → inline (NO buffer allocated)
        return exp;
    }
}
```

**Memory minimization principle:** Only signals that are genuinely shared or delayed get buffers. Single-use expressions are kept as inline computations, eliminating unnecessary intermediate storage.

---

## 5. Variable Storage by Variability Rate

**File:** `compiler/generator/instructions_compiler.cpp` (function `generateVariableStore`)

When a signal does need storage, the compiler chooses the most memory-efficient location based on its computation rate:

| Variability | Storage Strategy | Memory Impact |
|-------------|-----------------|---------------|
| `kKonst` (init-time) | DSP struct field | Computed once, persists |
| `kBlock` (control-rate) | Stack local in `control()` or packed into `iControl`/`fControl` array | Reused each block |
| `kSamp` (sample-rate) | Stack local in compute loop | Allocated/freed per sample block |

For `kBlock` variables with `-ec -mem1` options:
```cpp
case kBlock: {
    if (gGlobal->gExtControl && gGlobal->gMemoryManager >= 1) {
        // Pack into iControl/fControl arrays
        if (t->nature() == kInt) {
            pushControlDeclare(fContainer->fIntControl->store(exp));
            return fContainer->fIntControl->load();
        } else {
            pushControlDeclare(fContainer->fRealControl->store(exp));
            return fContainer->fRealControl->load();
        }
    }
}
```

This packs all control-rate intermediates into two contiguous arrays (`iControl[N]` and `fControl[M]`), eliminating per-variable struct overhead and improving cache locality.

---

## 6. Delay Line Strategy Selection: Memory vs. Performance Tradeoff

**File:** `compiler/generator/instructions_compiler.cpp` (function `generateDelayLine`)

The compiler selects from three delay line implementations based on the maximum delay value, each with different memory characteristics:

### Strategy 1: Copy-Based (delay < `gMaxCopyDelay`, default 16)
```
Buffer size: exactly (delay + 1) elements
Memory: minimal
Post-compute: shift array elements (O(delay) copies per sample)
```
```cpp
if (mxd < gGlobal->gMaxCopyDelay) {
    pushClearMethod(generateInitArray(vname, ctype, mxd + 1));
    pushComputeDSPMethod(IB::genStoreArrayStructVar(vname, 0, exp));
    pushPostComputeDSPMethod(generateShiftArray(vname, mxd));
}
```

### Strategy 2: Mask-Based Ring Buffer (delay < `gMaskDelayLineThreshold`)
```
Buffer size: pow2limit(delay + 1) — rounded up to power of 2
Memory: potentially wastes up to ~50% (e.g., delay=17 → buffer=32)
Access: bitwise AND masking (very fast, no branch)
```
```cpp
int N = pow2limit(mxd + 1);
FIRIndex value2 = (FIRIndex(IB::genLoadStructVar(fCurrentIOTA)) - CS(delay)) & FIRIndex(N - 1);
return IB::genLoadArrayStructVar(vname, value2);
```

### Strategy 3: Select-Based Ring Buffer (delay >= `gMaskDelayLineThreshold`)
```
Buffer size: exactly (delay + 1) elements
Memory: optimal (no waste)
Access: conditional wrap (slightly slower due to branch)
```
```cpp
FIRIndex widx_tmp2 = FIRIndex(IB::genLoadStackVar(widx_tmp_name));
pushPostComputeDSPMethod(IB::genStoreStackVar(widx_tmp_name,
    IB::genSelect2Inst(widx_tmp2 == FIRIndex(mxd + 1), FIRIndex(0), widx_tmp2)));
```

**The `-mcd` and `-dlt` flags** allow users to tune this tradeoff. Lower `-mcd` values favor memory; higher values favor speed. The `-dlt` flag controls when to switch from power-of-2 (memory-wasteful but fast) to exact-size (memory-optimal but slower) ring buffers.

---

## 7. DAG-Based Loop Scheduling and Fusion

**Files:** `compiler/parallelize/code_loop.hh`, `code_loop.cpp`, `compiler/DirectedGraph/Schedule.hh`

### 7.1 Loop DAG Structure

The Faust compiler represents the computation as a **DAG of loops** (`CodeLoop`). Each loop computes a subset of signals, and edges represent data dependencies:

```cpp
class CodeLoop {
    std::set<CodeLoop*> fBackwardLoopDependencies;  // Must compute before me
    std::set<CodeLoop*> fForwardLoopDependencies;   // Will compute after me
    int fUseCount;  // How many loops depend on this one
    ...
};
```

### 7.2 Loop Fusion (`groupSeqLoops`)

The `groupSeqLoops` optimization **merges sequential loops** that have a single consumer, eliminating the intermediate buffers between them:

```cpp
void CodeLoop::groupSeqLoops(CodeLoop* l, set<CodeLoop*>& visited)
{
    if (visited.find(l) == visited.end()) {
        visited.insert(l);
        int n = int(l->fBackwardLoopDependencies.size());
        if (n == 1) {
            CodeLoop* f = *(l->fBackwardLoopDependencies.begin());
            if (f->fUseCount == 1) {
                l->concat(f);  // MERGE: eliminates intermediate buffer
                groupSeqLoops(l, visited);
            }
        }
        ...
    }
}
```

**Memory impact:** When loop A feeds only into loop B, merging them eliminates the vector buffer that would otherwise connect them. Instead of:
```
Loop A: compute vec[0..N-1]
Loop B: read vec[0..N-1], compute result
```
We get:
```
Merged Loop: compute scalar, use immediately
```

This is the most significant buffer elimination optimization in the vectorized mode.

### 7.3 Scheduling Cost Function

The `Schedule.hh` template provides a scheduling cost metric that measures cache locality:

```cpp
template <typename N>
inline unsigned int schedulingcost(const digraph<N>& G, const schedule<N>& S)
{
    unsigned int cost = 0;
    for (const N& n : G.nodes()) {
        for (const auto& c : G.destinations(n)) {
            unsigned int t1 = S.order(n);
            unsigned int t0 = S.order(c.first);
            cost += (t1 - t0) * (t1 - t0);  // Squared distance
        }
    }
    return cost;
}
```

Lower cost means dependent computations are scheduled closer together, keeping intermediate values in cache rather than requiring larger buffers.

---

## 8. Zone-Based Memory Consolidation (iZone/fZone)

**Files:** `compiler/generator/instructions.hh` (struct `ZoneArray`), `code_container.cpp`

The `-mem1`/`-mem2`/`-mem3` modes implement a **static memory planning** strategy that consolidates all array fields into two flat arrays:

### 8.1 The ZoneArray Model

```cpp
struct ZoneArray {
    std::string fName;           // "iZone" or "fZone"
    int fCurIndex = 0;           // Current allocation offset
    int fDLThreshold = 0;        // Size threshold for internal vs external
    static int gInternalMemorySize;  // Remaining internal memory budget
    std::map<std::string, int> fMap;  // variable name → offset in zone
};
```

### 8.2 Allocation Decision

```cpp
DeclareVarInst* ZoneArray::declare(const string& vname, Typed* type, ValueInst* exp, bool is_static)
{
    int size = dynamic_cast<ArrayTyped*>(type)->fSize;
    if ((size <= fDLThreshold || size <= gInternalMemorySize) && !is_static) {
        // Keep in DSP struct (fast internal memory)
        gInternalMemorySize -= size;
        return IB::genDeclareVarInst(..., Address::kStruct, ...);
    } else {
        // Move to zone array (external memory)
        fMap[vname] = fCurIndex;
        fCurIndex += size;
        return IB::genNullDeclareVarInst();  // Remove from struct
    }
}
```

### 8.3 Access Rewriting

After allocation, all array accesses are rewritten to use zone offsets:

```cpp
// Before: fVec0[i] (individual struct array)
// After:  fZone[42 + i] (consolidated zone array)

Address* visit(IndexedAddress* indexed) {
    if (fArray->fMap.count(name) > 0) {
        return IB::genIndexedAddress(
            fArray->fName, fArray->fAccess,
            FIRIndex(fArray->fMap[name]) + indexed->getIndex()->clone(this));
    }
}
```

### 8.4 Memory Manager Modes

| Mode | Description | Memory Layout |
|------|-------------|---------------|
| `-mem` (0) | Custom allocator, arrays as pointers | DSP struct with pointer fields |
| `-mem1` | Zone model with explicit memory manager | `iControl[]` + `fControl[]` + `iZone[]` + `fZone[]` in struct |
| `-mem2` | Zone model, constants from/to memory | Same as mem1 + `constantsFromMem`/`constantsToMem` |
| `-mem3` | Zones as function parameters (FPGA/SYFALA) | Zones passed to `compute()` as args |

### 8.5 FPGA Memory Partitioning

The `-fpga-mem` and `-fpga-mem-th` options control the split between fast internal memory (block RAM) and slower external memory:

```cpp
// gFPGAMemory: total internal memory budget (in elements)
// gFPGAMemoryThreshold: per-array threshold for internal placement
ZoneArray::gInternalMemorySize = gGlobal->gFPGAMemory;
gGlobal->gIntZone = new ZoneArray("iZone", access, Typed::kInt32, gGlobal->gFPGAMemoryThreshold);
gGlobal->gRealZone = new ZoneArray("fZone", access, itfloat(), gGlobal->gFPGAMemoryThreshold);
```

Small arrays (≤ threshold or fitting in remaining budget) stay in the DSP struct (mapped to fast memory). Larger arrays are placed in the zone (mapped to external memory).

---

## 9. Subcontainer Inlining: Eliminating Intermediate Object Allocations

**File:** `compiler/generator/code_container.cpp` (function `inlineSubcontainersFunCalls`)

Faust uses "subcontainers" for lookup tables (waveforms, `rdtable`). Without inlining, each table requires a separate object allocation. The `-it` (inline table) option eliminates these:

```cpp
BlockInst* CodeContainer::inlineSubcontainersFunCalls(BlockInst* block)
{
    // Rename 'sig' in 'dsp' and remove 'dsp' allocation
    block = DspRenamer().getCode(block);

    // Inline subcontainers 'instanceInit' and 'fill' function call
    for (const auto& it : fSubContainers) {
        DeclareFunInst* inst_init_fun = it->generateInstanceInitFun(...);
        block = FunctionCallInliner(inst_init_fun).getCode(block);

        DeclareFunInst* fill_fun = it->generateFillFun(...);
        block = FunctionCallInliner(fill_fun).getCode(block);
    }
    ...
}
```

**Memory impact:** Eliminates N separate heap allocations (one per table) and replaces them with arrays directly in the DSP struct or zone. This is required for `-mem1`/`-mem2`/`-mem3` modes.

---

## 10. The DAG Instructions Compiler: Vectorized Buffer Management

**File:** `compiler/generator/dag_instructions_compiler.cpp`

The `DAGInstructionsCompiler` extends the scalar compiler with vector-aware caching. It uses three buffer naming conventions that reflect different memory roles:

| Prefix | Purpose | When Allocated |
|--------|---------|----------------|
| `Vec` | Non-sample signals used with delay | Delay line for slow signals |
| `Yec` | Sample-rate signals used with delay | Ring buffer for fast signals |
| `Zec` | Sample-rate signals, shared, no delay | Intermediate vector buffer |

```cpp
ValueInst* DAGInstructionsCompiler::generateCacheCode(Tree sig, ValueInst* exp)
{
    if (t->variability() < kSamp) {
        if (d > 0) {
            getTypedNames(..., "Vec", ctype, vname);  // Delay line
            ...
        }
    } else {
        if (d > 0) {
            getTypedNames(..., "Yec", ctype, vname);  // Sample-rate delay
            ...
        } else if (sharing > 1 && !verySimple(sig)) {
            getTypedNames(..., "Zec", ctype, vname);  // Shared vector
            ...
        } else {
            return exp;  // No buffer needed
        }
    }
}
```

### The `needSeparateLoop` Decision

This function determines when a signal needs its own loop (and thus a connecting buffer):

```cpp
bool DAGInstructionsCompiler::needSeparateLoop(Tree sig)
{
    if (o->getMaxDelay() > 0) return true;       // Delayed → separate loop
    if (verySimple(sig) || t->variability() < kSamp) return false;  // Trivial → inline
    if (isSigDelay(sig, x, y)) return false;      // Delay access → same loop
    if (isProj(sig, &i, x)) return true;          // Recursive → separate loop
    if (c > 1) return true;                        // Shared → separate loop
    return false;                                   // Default → inline
}
```

**Memory minimization:** Only signals that genuinely need temporal separation (delays, recursions, shared computations) get their own loops and buffers. Everything else is computed inline within the consuming loop.

---

## 11. One-Sample Mode: Zero-Buffer Computation

**Flag:** `-os` (one-sample)

The most aggressive memory optimization eliminates ALL intermediate buffers by processing one sample at a time:

```cpp
BlockInst* CodeLoop::generateOneSample()
{
    BlockInst* block = IB::genBlockInst();
    pushBlock(fPreInst, block);
    pushBlock(fComputeInst, block);
    pushBlock(fPostInst, block);
    ...
}
```

In this mode:
- No input/output buffer arrays are needed (single samples passed as scalars)
- No intermediate `Zec` vectors are allocated
- All computation is purely scalar
- Only delay lines (struct fields) persist between calls

This is ideal for FPGA targets and embedded systems where memory is extremely constrained.

---

## 12. Memory Layout Description and Custom Allocation

**File:** `compiler/generator/code_container.cpp` (function `createMemoryLayout`), `struct_manager.hh`

The compiler generates a complete **memory layout description** (`MemoryLayoutType`) that enables external memory managers to allocate DSP memory optimally:

```cpp
struct MemoryDesc {
    int fFieldIndex;     // Index in memory array
    int fOffset;         // Offset in bytes (mixed zone)
    int fIntOffset;      // Offset in int zone
    int fRealOffset;     // Offset in real zone
    int fRAccessCount;   // Read access counter
    int fWAccessCount;   // Write access counter
    int fSize;           // Size in frames
    int fSizeBytes;      // Size in bytes
    Typed::VarType fType;
    bool fIsConst;
    bool fIsControl;
    bool fIsScalar;
    memType fMemType;    // kLocal (fast) vs kExternal (slow)
};
```

The `createMemoryLayout` function:
1. Visits all struct declarations to compute field sizes
2. Generates a scalar loop to count read/write accesses per field
3. Produces a `MemoryLayoutItem` vector describing the complete allocation order

This information is exported in the JSON metadata, allowing runtime memory managers to:
- Place hot (frequently accessed) fields in fast memory
- Align arrays to cache line boundaries
- Pre-allocate the exact memory footprint

---

## 13. Vectorized Code: Buffer Lifetime and the DAG Loop

**File:** `compiler/generator/vec_code_container.cpp`

In vectorized mode (`-vec`), the compiler generates a two-level loop structure:

```
Outer loop: processes blocks of gVecSize samples (default 32)
    Inner DAG: topologically-sorted loops within each block
```

The `generateDAGLoopVariant0` function shows how buffers are scoped to the outer loop iteration:

```cpp
BlockInst* VectorCodeContainer::generateDAGLoopVariant0(const string& counter)
{
    // Outer loop over blocks of gVecSize
    for (vindex = 0; vindex <= fullcount - gVecSize; vindex += gVecSize) {
        int vsize = gVecSize;
        // Generate the DAG of inner loops
        generateDAGLoop(loop_code, size_dec->load());
    }
    // Handle remaining frames
    if (vindex < fullcount) {
        int vsize = fullcount - vindex;
        generateDAGLoop(then_block, size_dec1->load());
    }
}
```

**Buffer lifetime:** Intermediate `Zec` vectors only need to live for one block iteration (gVecSize samples). They are declared as stack variables in the outer loop body, so they are automatically reused across iterations without persistent allocation.

---

## 14. Summary of Memory Optimization Strategies

| Strategy | Mechanism | Memory Savings |
|----------|-----------|----------------|
| **Sharing analysis** | Inline single-use expressions | Eliminates unnecessary buffers |
| **Occurrence-driven caching** | Only materialize delayed/shared signals | Minimal buffer count |
| **Loop fusion** | Merge sequential single-consumer loops | Eliminates inter-loop vectors |
| **Delay line selection** | Choose copy/mask/select based on delay size | Optimal buffer sizing |
| **Zone consolidation** | Pack arrays into iZone/fZone | Single allocation, better locality |
| **Control packing** | Pack control vars into iControl/fControl | Eliminates per-variable overhead |
| **Subcontainer inlining** | Inline table init code | Eliminates object allocations |
| **One-sample mode** | Process single samples | Zero intermediate buffers |
| **FPGA memory partitioning** | Budget-based internal/external split | Fits fast memory constraints |
| **Variability-based placement** | Stack for temps, struct for persistent | Automatic lifetime management |
| **Scheduling cost** | Minimize distance between dependent nodes | Better cache utilization |

---

## 15. Compiler Flags Controlling Memory Behavior

| Flag | Default | Effect |
|------|---------|--------|
| `-mcd N` | 16 | Max delay for copy-based (small buffer) strategy |
| `-dlt N` | INT_MAX | Threshold for mask vs. select ring buffer |
| `-vec` | off | Enable vectorized mode (block processing) |
| `-vs N` | 32 | Vector size (block size in samples) |
| `-os` | off | One-sample mode (no intermediate buffers) |
| `-mem` | off | Custom memory manager mode |
| `-mem1` | off | Zone-based memory with explicit manager |
| `-mem2` | off | Zone-based with constants from/to memory |
| `-mem3` | off | Zones as function parameters (FPGA) |
| `-it` | off | Inline tables (required for -mem1/2/3) |
| `-ec` | off | External control (iControl/fControl packing) |
| `-fpga-mem N` | 0 | Internal memory budget (elements) |
| `-fpga-mem-th N` | 4 | Per-array threshold for internal placement |
| `-g` | off | Group sequential tasks (enables loop fusion) |

---

## 16. Architectural Insight

The Faust compiler's approach to memory optimization is fundamentally **DAG-driven and declarative** rather than imperative:

1. The signal graph IS the DAG — no separate "memory planning" pass constructs a DAG; the signal tree itself encodes all data dependencies.

2. **Buffer allocation is demand-driven**: buffers are only created when sharing analysis or delay requirements mandate them. The default is "no buffer" (inline computation).

3. **Lifetime is implicit in variability**: the type system's variability classification (kKonst/kBlock/kSamp) directly maps to storage duration (persistent/per-block/per-sample), eliminating the need for explicit lifetime analysis.

4. **The zone model is a static memory plan**: by consolidating all arrays into two flat zones with compile-time offsets, the compiler produces what is effectively a static memory allocation plan — no runtime allocator needed for the DSP state.

This design is particularly well-suited to real-time audio where deterministic memory behavior (no allocation in the audio thread) is a hard requirement.