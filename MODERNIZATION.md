# FFmpeg Modernization Guide

This document describes the ongoing effort to modernize FFmpeg with CMake and selective C++ features.

## Philosophy

Our modernization approach is **pragmatic and performance-focused**:

- ✅ Use modern C++ features that provide **zero-overhead** abstraction
- ✅ Selectively convert files where modernization provides **clear benefits**
- ✅ Maintain **backward compatibility** and performance characteristics
- ❌ **Avoid** heavy abstraction, RAII (for now), STL containers, exceptions
- ❌ **Avoid** converting files where C works perfectly fine

## Build System: CMake

### Why CMake?

- Modern, widely-supported build system
- Better IDE integration (CLion, VS Code, Visual Studio)
- Easier cross-platform development
- Cleaner dependency management
- Mixed C/C++ support out of the box

### Project Structure

```
FFmpeg/
├── CMakeLists.txt              # Root build configuration
├── libavutil/CMakeLists.txt    # Per-library build files
├── libavcodec/CMakeLists.txt
├── libavformat/CMakeLists.txt
└── ...
```

### Building with CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Configuration Options

```cmake
# Core libraries (ON by default)
-DCONFIG_AVCODEC=ON
-DCONFIG_AVFORMAT=ON
-DCONFIG_AVFILTER=ON
-DCONFIG_AVDEVICE=ON
-DCONFIG_SWSCALE=ON
-DCONFIG_SWRESAMPLE=ON

# Hardware acceleration
-DCONFIG_CUDA=OFF
-DCONFIG_VULKAN=OFF
-DCONFIG_VAAPI=OFF
```

## Language Standards

- **C files (.c)**: C17 standard
- **C++ files (.cpp/.hpp)**: C++20 standard
- Per-file language selection (automatic based on extension)

## Modern C++ Features in Use

### 1. constexpr Functions (Zero Runtime Cost)

Functions marked `constexpr` can be evaluated at **compile time** when inputs are compile-time constants.

**Example:**
```cpp
// Traditional C code - computed at runtime
AVRational duration = av_mul_q(frame_rate, time_base);

// Modern C++ - can be computed at compile time!
constexpr AVRational frame_rate = {24, 1};
constexpr AVRational time_base = {1, 48000};
constexpr AVRational duration = ffmpeg::mul_q(frame_rate, time_base);
// duration is computed by compiler, zero runtime cost!
```

**Benefits:**
- Faster code (computation done at compile time)
- Catch errors earlier (at compile time vs runtime)
- Self-documenting (constexpr signals intent)

**Files demonstrating this:**
- `libavutil/rational_constexpr.hpp`

### 2. nullptr instead of NULL

**Before (C/Old C++):**
```c
AVFrame *frame = NULL;
if (frame == NULL) return -1;
```

**After (Modern C++):**
```cpp
AVFrame *frame = nullptr;
if (frame == nullptr) return -1;
```

**Benefits:**
- Type-safe (nullptr has type `std::nullptr_t`)
- Prevents accidental integer conversion bugs
- More explicit intent

**Files updated:**
- `libavfilter/dnn/dnn_backend_torch.cpp`

### 3. static_cast instead of C-style Casts

**Before:**
```c
THModel *model = (THModel *)task->model;
void *data = (void *)buffer;
```

**After:**
```cpp
auto *model = static_cast<THModel *>(task->model);
auto *data = static_cast<void *>(buffer);
```

**Benefits:**
- Searchable (grep for "static_cast")
- Explicit cast type (static vs dynamic vs reinterpret)
- Caught by compiler if invalid

### 4. auto Type Inference

**Before:**
```cpp
THRequestItem *item = (THRequestItem *)ff_safe_queue_pop_front(queue);
```

**After:**
```cpp
auto *item = static_cast<THRequestItem *>(ff_safe_queue_pop_front(queue));
```

**Benefits:**
- Reduces verbosity without losing type information
- Easier refactoring (type changes propagate)
- Pointer/reference qualification still explicit (auto*)

### 5. enum class for Type Safety

**Before:**
```c
#define AV_ROUND_ZERO     0
#define AV_ROUND_INF      1
#define AV_ROUND_DOWN     2

int mode = AV_ROUND_ZERO;
mode = 5; // Oops! No error
```

**After:**
```cpp
enum class RoundMode : int {
    Zero = 0,
    Inf  = 1,
    Down = 2,
};

RoundMode mode = RoundMode::Zero;
mode = 5; // Compiler error! Type-safe
mode = RoundMode::Inf; // Must use scoped name
```

**Benefits:**
- No implicit integer conversion
- Scoped names (no global namespace pollution)
- Explicit underlying type

## Files Modernized

### ✅ Completed

1. **CMake Build System**
   - Root `CMakeLists.txt`
   - Per-library CMake configuration
   - Support for mixed C/C++ compilation

2. **libavfilter/dnn/dnn_backend_torch.cpp**
   - Replaced NULL → nullptr
   - C-style casts → static_cast
   - Added auto type inference
   - Removed unnecessary parentheses in delete

3. **libavutil/rational_constexpr.hpp** (NEW)
   - Compile-time rational arithmetic
   - constexpr GCD algorithm
   - Type-safe enums
   - Static assertions
   - Example of modern C++ benefits

### 🔄 Good Candidates for Future Conversion

1. **Math/DSP utilities** (`libavutil/`)
   - `mathematics.c` → constexpr functions for compile-time math
   - `fixed_dsp.c` → template-based fixed-point arithmetic
   - `integer.c` → constexpr integer operations

2. **SIMD helpers** (`libavcodec/`)
   - Template-based type dispatch
   - constexpr selection of optimal algorithm

3. **Configuration parsing** (`libavutil/opt.c`)
   - enum class for option types
   - constexpr validation

### ❌ Keep as C

Most of FFmpeg should stay as C:
- Performance-critical codec implementations
- Assembly-heavy code (SIMD kernels)
- Public API (must remain C for compatibility)
- Code that doesn't benefit from C++ features

## Guidelines for Contributors

### When to Use C++

1. **Math utilities** - Use constexpr for compile-time computation
2. **Type safety** - Use enum class instead of #define enums
3. **Existing C++ code** - Modernize to C++20 standards
4. **Generic algorithms** - Templates can replace macros

### When to Keep C

1. **Public APIs** - Must remain C for ABI compatibility
2. **Hot paths** - Keep C if C++ adds overhead
3. **Simple code** - Don't convert just for the sake of it
4. **Working code** - "If it ain't broke, don't fix it"

### Code Style

```cpp
// ✅ GOOD: Zero-overhead modern C++
constexpr int compute_size(int width, int height) {
    return width * height;
}

// ✅ GOOD: Type-safe without overhead
enum class PixelFormat : int {
    YUV420P,
    RGB24,
};

// ❌ BAD: Unnecessary abstraction
class FrameWrapper {
    std::unique_ptr<AVFrame> frame;  // RAII overhead
};

// ❌ BAD: STL containers in hot paths
std::vector<AVPacket> packets;  // Heap allocations!
```

## Performance Validation

All modernization changes must:
1. Pass existing test suite
2. Show no performance regression
3. Generate equivalent or better assembly code

### Checking Assembly Output

```bash
# Compile with assembly output
gcc -S -O3 -fverbose-asm file.c -o file_c.s
g++ -S -O3 -fverbose-asm file.cpp -o file_cpp.s
diff file_c.s file_cpp.s
```

## Testing

```bash
# Build with CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build all libraries
cmake --build .

# Run tests (when implemented)
ctest
```

## Benefits of This Approach

1. **Gradual Migration** - Convert files incrementally, no big bang
2. **Performance First** - Only use zero-overhead features
3. **Compatibility** - Public API remains C, existing code continues to work
4. **Developer Experience** - Better tooling, IDE support, compile-time errors
5. **Maintainability** - Safer code with type checking, constexpr validation

## Roadmap

### Phase 1 (Current)
- ✅ CMake build system
- ✅ Modernize existing C++ files
- ✅ Create constexpr utility examples

### Phase 2 (Next)
- Convert math utilities to use constexpr
- Add comprehensive CMake configuration options
- Integration with existing configure script

### Phase 3 (Future)
- Template-based SIMD dispatch
- Expand constexpr usage
- Performance validation suite

## Questions?

See also:
- `libavutil/rational_constexpr.hpp` - Example of modern C++ features
- Root `CMakeLists.txt` - Build system configuration
- This document for philosophy and guidelines

---

**Remember**: We're modernizing selectively and pragmatically. The goal is better code, not just different code.
