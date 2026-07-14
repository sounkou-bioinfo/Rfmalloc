# Makefile for the vendored GGML CPU backend.
#
# Invoked by ../../configure (which exports CC/CXX/AR/RANLIB/CFLAGS/CXXFLAGS
# using R's own toolchain settings, mirroring inst/fmalloc/Makefile in the
# Rfmalloc package). Produces libggml.a, which src/Makevars statically links
# into Rggml.so. Downstream packages never see this Makefile: they consume
# the installed headers in inst/include/ and the registered C-callables.
#
# The portable CPU baseline uses GGML_CPU_GENERIC. On x86, configure stages
# selected kernels under ISA flags and runtime dispatch owns their canonical
# symbols. On aarch64, configure clears GENERIC_DEFS and builds GGML's NEON
# kernels directly. Dense products use the BLAS backend and offload F32
# mul_mat to R's own BLAS through the cblas-to-Fortran shim below. Device
# backends are built only when configure receives --with-vulkan or --with-cuda.

AR     ?= ar
RANLIB ?= ranlib
CC     ?= cc
CXX    ?= c++

STATICLIB = libggml.a

INCLUDES = -I. -Iggml-cpu -I../include

# -include rggml_compat.h redirects stdio/stderr/abort/exit in the vendored
# GGML sources to R-safe equivalents implemented in rggml_io.c (CRAN policy:
# no direct console writes, no process-terminating calls from library code).
# -DNDEBUG compiles out C-library assert(); without it, macOS's assert()
# expands to __assert_rtn, which R CMD check flags (compiled code must not call
# process-terminating entry points). GGML's own GGML_ASSERT is separately
# redirected via rggml_compat.h, so this only removes libc assert().
# -DGGML_USE_BLAS enables GGML's BLAS backend (ggml-blas.cpp). Its cblas_sgemm
# calls are satisfied by our portable shim (cblas.h + rggml_cblas.c) forwarding
# to R's Fortran sgemm_; the final Rggml.so links $(BLAS_LIBS) $(FLIBS).
# On aarch64, ../../configure clears GENERIC_DEFS and passes ARCH_OBJECTS instead:
# GGML's own hand-tuned NEON vec_dot kernels (ggml-cpu/arch/arm/quants.c) then
# provide the canonical symbols, and arch-fallback.h's aarch64 section maps the
# handful they don't implement onto the generic code. NEON is a mandatory
# baseline on aarch64, so this needs no ISA flag and cannot break portability -
# unlike arch/x86, whose kernels are gated on compile-time __AVX2__ and would
# make the binary refuse to run on machines without it.
GENERIC_DEFS ?= -DGGML_CPU_GENERIC
ARCH_OBJECTS ?=

DEFS = -DGGML_USE_CPU $(GENERIC_DEFS) -DGGML_USE_BLAS -DGGML_R_PACKAGE -D_GNU_SOURCE -DNDEBUG \
       -DGGML_VERSION='"0.11.0"' -DGGML_COMMIT='"1f09c6987071"' \
       -include rggml_compat.h

# Vulkan backend: empty unless ../../configure was run with --with-vulkan, in
# which case it passes the objects (ggml-vulkan.o plus one TU per generated
# SPIR-V shader) and the flags. -DGGML_USE_VULKAN must reach every TU, not just
# the Vulkan ones: ggml-backend-reg.cpp registers the backend behind that define.
VULKAN_OBJECTS ?=
VULKAN_CPPFLAGS ?=

# CUDA source selection mirrors upstream GGML 0.11.0's CMake recipe. The
# default build leaves CUDA_ENABLED at zero, so nvcc is neither detected nor
# invoked unless configure received --with-cuda.
CUDA_ENABLED ?= 0
NVCC ?= nvcc
CUDA_CPPFLAGS ?=
CUDA_NVCCFLAGS ?=
CUDA_HOST_PIC ?= -fPIC
CUDA_SOURCES =
ifeq ($(CUDA_ENABLED),1)
CUDA_SOURCES += $(wildcard ggml-cuda/*.cu)
CUDA_SOURCES += $(wildcard ggml-cuda/template-instances/fattn-tile*.cu)
CUDA_SOURCES += $(wildcard ggml-cuda/template-instances/fattn-mma*.cu)
CUDA_SOURCES += $(wildcard ggml-cuda/template-instances/mmq*.cu)
CUDA_SOURCES += $(wildcard ggml-cuda/template-instances/mmf*.cu)
CUDA_SOURCES += ggml-cuda/template-instances/fattn-vec-instance-f16-f16.cu
CUDA_SOURCES += ggml-cuda/template-instances/fattn-vec-instance-q4_0-q4_0.cu
CUDA_SOURCES += ggml-cuda/template-instances/fattn-vec-instance-q8_0-q8_0.cu
CUDA_SOURCES += ggml-cuda/template-instances/fattn-vec-instance-bf16-bf16.cu
endif
CUDA_OBJECTS = $(CUDA_SOURCES:.cu=.o)

ALL_CFLAGS   = $(CPPFLAGS) $(INCLUDES) $(DEFS) $(VULKAN_CPPFLAGS) $(CUDA_CPPFLAGS) $(CFLAGS)
ALL_CXXFLAGS = $(CPPFLAGS) $(INCLUDES) $(DEFS) $(VULKAN_CPPFLAGS) $(CUDA_CPPFLAGS) $(CXXFLAGS) -std=gnu++17

C_SOURCES = \
  ggml.c ggml-alloc.c ggml-quants.c \
  ggml-cpu/quants.c

CXX_SOURCES = \
  ggml-backend.cpp ggml-backend-reg.cpp ggml-backend-meta.cpp ggml-threading.cpp gguf.cpp \
  ggml-cpu/traits.cpp ggml-cpu/vec.cpp ggml-cpu/repack.cpp \
  ggml-cpu/binary-ops.cpp ggml-cpu/unary-ops.cpp ggml-cpu/ops.cpp \
  ggml-blas.cpp

# The upstream CPU backend has ggml-cpu.c and ggml-cpu.cpp in one directory.
# A suffix-only make rule would map both to ggml-cpu.o, so name their objects
# explicitly instead of silently dropping half the backend from the archive.
CPU_CORE_OBJECTS = ggml-cpu/ggml-cpu-c.o ggml-cpu/ggml-cpu-cpp.o

# SIMD runtime dispatch (RsimdDispatch strategy): cpu_features (CPUID detect)
# and the dispatcher are always built; per-ISA kernel variants
# ($(SIMD_EXTRA_OBJECTS), e.g. rggml_q4k_avx2.o) are staged conditionally by
# ../../configure after it probes toolchain ISA support. SIMD_DISPATCH_DEFS
# tells the dispatcher which staged variants exist (e.g. -DRGGML_HAVE_AVX2).
# The ISA flags live here/in configure, never in R's recorded package flags,
# so R CMD check raises no "non-portable flags" NOTE.
SIMD_DIR = ../../tools/simd
SIMD_EXTRA_OBJECTS ?=
SIMD_DISPATCH_DEFS ?=
# Emptied by configure on aarch64: GGML's arch/arm/quants.c already defines the
# canonical ggml_vec_dot_q4_K_q8_K, which our dispatcher would duplicate.
DISPATCH_OBJECTS ?= cpu_features.o rggml_simd_dispatch.o

OBJECTS = $(C_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o) $(CPU_CORE_OBJECTS) \
          rggml_io.o rggml_cblas.o \
          $(DISPATCH_OBJECTS) $(SIMD_EXTRA_OBJECTS) $(ARCH_OBJECTS) \
          $(VULKAN_OBJECTS) $(CUDA_OBJECTS)

.PHONY: all clean

all: $(STATICLIB)

$(STATICLIB): $(OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(OBJECTS)
	-$(RANLIB) $@

# rggml_io.c implements the R-safe replacements themselves: it must NOT get
# the macro redirections from rggml_compat.h (it includes R.h directly, and
# the format(printf,...) attributes on R's own printf-like declarations
# would otherwise be mangled by the `#define printf ...` redirection).
rggml_io.o: rggml_io.c
	$(CC) $(ALL_CFLAGS) $(R_INCLUDE_FLAGS) -DR_GGML_IO_IMPL -c $< -o $@

# rggml_cblas.c implements cblas_sgemm() over R's Fortran BLAS. It needs R's
# headers (F77_NAME from <R_ext/RS.h>) but must NOT get the rggml_compat.h
# redirections, so it is built with a plain flag set (INCLUDES gives it cblas.h).
# CBLAS_DEFS carries -DRGGML_HAVE_SGEMM when ../../configure link-probed sgemm_
# successfully; otherwise the shim promotes to dgemm_, which R always provides.
CBLAS_DEFS ?=
rggml_cblas.o: rggml_cblas.c
	$(CC) $(CPPFLAGS) $(INCLUDES) $(R_INCLUDE_FLAGS) $(CBLAS_DEFS) $(CFLAGS) -c $< -o $@

ggml-cpu/ggml-cpu-c.o: ggml-cpu/ggml-cpu.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

ggml-cpu/ggml-cpu-cpp.o: ggml-cpu/ggml-cpu.cpp
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

# --- SIMD runtime dispatch (sources under ../../tools/simd) ------------------
# cpu_features.c: pure CPUID/runtime detection, no GGML or R dependencies.
cpu_features.o: $(SIMD_DIR)/cpu_features.c
	$(CC) $(CPPFLAGS) -I$(SIMD_DIR) $(CFLAGS) -c $< -o $@

# The dispatcher owns the canonical ggml_vec_dot_* symbols and forwards to the
# scalar generic or a staged ISA variant. SIMD_DISPATCH_DEFS (set by configure)
# is -DRGGML_HAVE_* for each staged variant.
rggml_simd_dispatch.o: $(SIMD_DIR)/rggml_simd_dispatch.c
	$(CC) $(CPPFLAGS) -I$(SIMD_DIR) $(SIMD_DISPATCH_DEFS) $(CFLAGS) -c $< -o $@

# AVX2 variant of the q4_K x q8_K dot (needs the GGML includes/defines). Built
# only when configure staged it; -mavx2 -mfma -O3 auto-vectorizes the loops.
rggml_q4k_avx2.o: $(SIMD_DIR)/vec_dot_q4_K.c
	$(CC) $(ALL_CFLAGS) -DRGGML_Q4K_DOT_FN=rggml_vec_dot_q4_K_q8_K_avx2 \
	    -mavx2 -mfma -O3 -funroll-loops -c $< -o $@

# NEON variant of the q4_K x q8_K dot. NEON is a mandatory baseline on aarch64,
# so no ISA flag is needed - just -O3 to auto-vectorize better than the -O2
# GGML_CPU_GENERIC scalar build.
rggml_q4k_neon.o: $(SIMD_DIR)/vec_dot_q4_K.c
	$(CC) $(ALL_CFLAGS) -DRGGML_Q4K_DOT_FN=rggml_vec_dot_q4_K_q8_K_neon \
	    -O3 -funroll-loops -c $< -o $@

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

# Do not force-include rggml_compat.h in nvcc. CUDA has legitimate device-side
# printf calls; redirecting those to an R host function makes the device pass
# invalid. Host diagnostics use GGML's logging callback, and R CMD check audits
# the linked shared object for forbidden process and console calls.
%.o: %.cu
	$(NVCC) $(CUDA_NVCCFLAGS) $(CUDA_CPPFLAGS) -I. -I../include -Iggml-cuda \
	    -DGGML_CUDA_PEER_MAX_BATCH_SIZE=128 -Xcompiler "$(CUDA_HOST_PIC)" \
	    -c $< -o $@

clean:
	rm -f $(STATICLIB) $(OBJECTS)
