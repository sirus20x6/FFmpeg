# C to C++ Conversion Candidates

This document lists C files that are good candidates for selective conversion to modern C++. Files are categorized by priority based on the potential benefits from C++ features.

## Priority System

- **HIGH** - Clear, significant benefits from C++ features (constexpr, type safety, zero overhead)
- **MEDIUM** - Moderate benefits, requires careful evaluation
- **LOW** - Minor benefits, low priority for conversion
- **NO** - Should NOT be converted (performance-critical, public API, assembly-heavy)

## Evaluation Criteria

✅ **Good Candidates:**
- Lookup table generation (constexpr arrays)
- Pure mathematical functions (constexpr computation)
- Type-heavy code (enum class, strong typing)
- Configuration/options (type safety)
- Small utility functions (<10KB)
- No side effects, pure functions

❌ **Bad Candidates:**
- Public API functions (must stay C)
- Hot codec paths (no conversion overhead)
- Assembly-heavy code
- Platform-specific implementations
- Large, complex state machines
- Files with heavy I/O or system calls

---

## HIGH Priority Candidates

### 1. libavcodec/mathtables.c (12KB)
**Priority: HIGH** ⭐⭐⭐⭐⭐

**Why Convert:**
- Contains 6 lookup tables that are perfect for constexpr generation:
  - `ff_square_tab[512]` - (i-256)² lookups
  - `ff_inverse[257]` - division approximations
  - `ff_sqrt_tab[256]` - integer square root
  - `ff_crop_tab[]` - value clamping
  - `ff_zigzag_direct[64]` - JPEG zigzag scan order
  - `ff_zigzag_scan[17]`, `ff_log2_run[41]` - encoding tables
- All tables can be generated at compile time
- Zero runtime initialization cost
- Tables are used throughout codec implementations

**Conversion Benefits:**
- Tables generated at compile time with constexpr functions
- Compile-time validation with static_assert
- Type safety with enum class for indices
- No runtime initialization overhead
- Example: `constexpr uint32_t square(int i) { return (i-256)*(i-256); }`

**Estimated Effort:** Medium (1-2 hours)
**Risk:** Very Low (pure data, heavily tested by existing usage)

---

### 2. libavutil/log2_tab.c (33 lines)
**Priority: HIGH** ⭐⭐⭐⭐⭐

**Why Convert:**
- Single lookup table: `ff_log2_tab[256]`
- Perfect constexpr candidate
- Used in performance-critical code paths
- Currently a static array, can be compile-time generated

**Conversion Benefits:**
```cpp
constexpr uint8_t compute_log2(int i) noexcept {
    // Compute log2 at compile time
    return i == 0 ? 0 : (i == 1 ? 0 : 1 + compute_log2(i/2));
}

constexpr auto generate_log2_table() noexcept {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; i++)
        table[i] = compute_log2(i);
    return table;
}

constexpr auto ff_log2_tab = generate_log2_table();
```

**Estimated Effort:** Low (30 minutes)
**Risk:** Very Low (trivial conversion, easy to validate)

---

### 3. libavutil/integer.c (167 lines)
**Priority: HIGH** ⭐⭐⭐⭐

**Why Convert:**
- Arbitrary precision integer arithmetic
- Pure mathematical operations, no side effects
- Functions: av_add_i, av_sub_i, av_mul_i, av_div_i, av_mod_i
- All operations can be constexpr
- Could benefit from C++ operator overloading

**Conversion Benefits:**
- Constexpr functions allow compile-time computation
- Operator overloading for natural syntax: `a + b` instead of `av_add_i(a, b)`
- Type safety with dedicated `AVInteger` class wrapper
- Template-based implementations for different precisions
- Static assertions for compile-time validation

**Estimated Effort:** Medium (2-3 hours)
**Risk:** Low (pure math, well-defined behavior)

---

### 4. libavcodec/celp_math.c (122 lines)
**Priority: HIGH** ⭐⭐⭐⭐

**Why Convert:**
- Fixed-point mathematical operations for CELP codecs
- Contains lookup tables: `exp2a[32]`, `exp2b[32]`, `tab_log2[33]`
- Pure functions: `ff_exp2()`, `ff_log2_q15()`, `ff_dot_product()`
- All can be constexpr

**Conversion Benefits:**
- Lookup tables generated at compile time
- Constexpr exp2/log2 functions
- Template-based dot product for type flexibility
- Compile-time validation of table values
- Type-safe enums for G.729 bitexact mode

**Estimated Effort:** Medium (2 hours)
**Risk:** Low (pure math, isolated functionality)

---

### 5. libavcodec/cbrt_tablegen_template.c (44 lines)
**Priority: HIGH** ⭐⭐⭐⭐

**Why Convert:**
- Table generator for cube root approximations
- Currently uses runtime generation, perfect for constexpr
- Template-based (USE_FIXED macro determines float/fixed)

**Conversion Benefits:**
- Cube root computed at compile time
- No runtime table initialization overhead
- C++20 templates replace C macros
- Type-safe float/fixed point selection
- Example:
```cpp
template<bool UseFixed>
constexpr auto generate_cbrt_table() noexcept {
    using T = std::conditional_t<UseFixed, uint32_t, float>;
    std::array<T, TABLE_SIZE> table{};
    for (int i = 0; i < TABLE_SIZE; i++)
        table[i] = compute_cbrt<UseFixed>(i);
    return table;
}
```

**Estimated Effort:** Medium (1-2 hours)
**Risk:** Low (isolated, well-tested functionality)

---

### 6. libavutil/fixed_dsp.c (174 lines)
**Priority: HIGH** ⭐⭐⭐⭐

**Why Convert:**
- Fixed-point DSP operations
- Functions: `vector_fmul()`, `vector_fmul_add()`, `scalarproduct_fixed()`
- Currently uses function pointers for dispatch
- Simple loops, good candidates for templates

**Conversion Benefits:**
- Templates for type-safe fixed-point operations
- Constexpr where applicable
- Better inlining opportunities
- Type safety prevents mixing fixed/float types
- Example:
```cpp
template<typename T>
constexpr void vector_fmul(T* dst, const T* src0, const T* src1, int len) noexcept {
    for (int i = 0; i < len; i++) {
        int64_t accu = static_cast<int64_t>(src0[i]) * src1[i];
        dst[i] = static_cast<T>((accu + 0x40000000) >> 31);
    }
}
```

**Estimated Effort:** Medium (2-3 hours)
**Risk:** Medium (need to preserve SIMD optimizations)

---

### 7. libavutil/error.c (134 lines)
**Priority: MEDIUM-HIGH** ⭐⭐⭐⭐

**Why Convert:**
- Error code to string mapping
- Static lookup table: `error_entries[]`
- Linear search through entries (could use C++ map/unordered_map)

**Conversion Benefits:**
- Constexpr error table
- `enum class` for error codes (type safety)
- Compile-time validation of error mappings
- Could use `std::string_view` for zero-copy strings (C++17)
- Better search algorithm (but table is small, ~40 entries)

**Estimated Effort:** Medium (1-2 hours)
**Risk:** Low (isolated functionality, easy to test)

**Note:** This is less critical than math/table files, but still beneficial.

---

## MEDIUM Priority Candidates

### 8. Table Generator Files (15 files, <2KB each)
**Priority: MEDIUM** ⭐⭐⭐

**Files:**
- `libavcodec/aacps_tablegen.c`
- `libavcodec/aacps_fixed_tablegen.c`
- `libavcodec/cbrt_fixed_tablegen.c`
- `libavcodec/dv_tablegen.c`
- `libavcodec/motionpixels_tablegen.c`
- `libavcodec/mpegaudio_tablegen.c`
- `libavcodec/mpegaudiodec_common_tablegen.c`
- `libavcodec/pcm_tablegen.c`
- `libavcodec/qdm2_tablegen.c`
- `libavcodec/sinewin_tablegen.c`
- `libavcodec/sinewin_fixed_tablegen.c`

**Why Convert:**
- All generate lookup tables at compile time
- Perfect use case for constexpr
- Currently use preprocessor meta-programming
- Eliminate runtime table initialization

**Conversion Benefits:**
- Replace C macros with C++20 templates
- Constexpr table generation
- Type-safe table access
- Compile-time validation

**Estimated Effort:** High (10-15 hours total, but can be done incrementally)
**Risk:** Medium (need to ensure bit-exact output for codec compliance)

---

### 9. Small DSP Init Files (<10KB)
**Priority: MEDIUM** ⭐⭐⭐

**Files:**
- `libavcodec/huffyuvdsp.c` (2.8KB)
- `libavcodec/mss2dsp.c` (5.8KB)
- `libavcodec/mss34dsp.c` (3.6KB)
- `libavcodec/h264dsp.c` (6.0KB)

**Why Convert:**
- DSP function pointer initialization
- Could use templates for type dispatch
- Small, isolated files

**Conversion Benefits:**
- Templates for type-based dispatch
- Constexpr where possible
- Better type safety

**Estimated Effort:** Medium (1-2 hours each)
**Risk:** Medium (must not break SIMD optimizations)

**Note:** These are less beneficial than pure math/table files.

---

## LOW Priority Candidates

### 10. libavutil/hash.c (7.5KB)
**Priority: LOW** ⭐⭐

**Why:**
- Hash algorithm dispatch
- Could use C++ polymorphism
- But current C implementation is fine

**Benefits:** Marginal
**Effort:** Medium
**Risk:** Low

---

### 11. libavutil/crc.c (25KB)
**Priority: LOW** ⭐⭐

**Why:**
- CRC table generation and computation
- Could use constexpr for table generation
- But file is large and complex

**Benefits:** Some (constexpr CRC tables)
**Effort:** High
**Risk:** Medium

---

## Files to NOT Convert

### ❌ Codec Implementations
**Examples:** h264.c, hevc.c, vp9.c, etc.
**Reason:** Performance-critical, heavily optimized, assembly implementations

### ❌ Public API Headers
**Examples:** avcodec.h, avformat.h, avutil.h
**Reason:** Must remain C for ABI compatibility

### ❌ Large State Machines
**Examples:** Most demuxers/muxers, format parsers
**Reason:** Complex control flow, not pure functions

### ❌ Platform-Specific Code
**Examples:** OS-specific I/O, threading primitives
**Reason:** System-dependent, C is appropriate

### ❌ SIMD Implementations
**Examples:** *_neon.c, *_sse.c, *_avx2.c
**Reason:** Assembly-heavy, hand-optimized

---

## Recommended Conversion Order

Based on effort/benefit ratio:

1. ✅ **libavutil/log2_tab.c** - Easiest, immediate benefit (30 min)
2. ✅ **libavcodec/mathtables.c** - High impact, clear benefits (2 hours)
3. ✅ **libavutil/integer.c** - Good showcase of C++ features (3 hours)
4. ✅ **libavcodec/celp_math.c** - Math utilities (2 hours)
5. ✅ **libavutil/fixed_dsp.c** - Templates demo (3 hours)
6. ✅ **libavcodec/cbrt_tablegen_template.c** - Constexpr tables (2 hours)
7. ⏸️ **libavutil/error.c** - Nice to have (2 hours)
8. ⏸️ **Other tablegen files** - Incremental (1-2 hours each)

**Total High Priority Effort:** ~15-20 hours
**Expected Benefit:** Compile-time table generation, zero runtime cost, better type safety

---

## Conversion Guidelines

When converting these files:

1. **Preserve Behavior:** Bit-exact output for codec compliance
2. **Add Tests:** Validate against original C implementation
3. **Use Constexpr:** Maximize compile-time computation
4. **Static Assert:** Add compile-time validation
5. **Namespaces:** Use `ffmpeg::` namespace
6. **No Exceptions:** Keep `noexcept` everywhere
7. **No RAII:** Follow project guidelines (for now)
8. **No STL Containers:** Use C arrays or `std::array` only
9. **Document:** Explain why C++ is beneficial for each file

---

## Benefits Summary

### Performance Benefits:
- ⚡ Compile-time table generation (zero runtime init)
- ⚡ Better inlining opportunities
- ⚡ Constexpr evaluation eliminates runtime computation

### Safety Benefits:
- 🛡️ Type safety with `enum class`
- 🛡️ Strong typing prevents implicit conversions
- 🛡️ Compile-time validation with `static_assert`
- 🛡️ `nullptr` instead of NULL

### Maintainability Benefits:
- 📖 Templates replace preprocessor macros
- 📖 Clearer type information
- 📖 Better IDE support and refactoring
- 📖 Self-documenting code with types

---

## Next Steps

1. Start with `log2_tab.c` as proof-of-concept
2. Convert `mathtables.c` to demonstrate table generation
3. Create test suite validating C++ output matches C
4. Document patterns for future conversions
5. Continue with remaining high-priority files

---

*Document created: 2025-11-07*
*Total candidates identified: 11 high/medium priority files*
*Estimated total effort: 25-35 hours for all high priority conversions*
