# Makefile for the vendored GGML CPU backend.
#
# Invoked by ../../configure (which exports CC/CXX/AR/RANLIB/CFLAGS/CXXFLAGS
# using R's own toolchain settings, mirroring inst/fmalloc/Makefile in the
# Rfmalloc package). Produces libggml.a, which src/Makevars statically links
# into Rggml.so. Downstream packages never see this Makefile: they consume
# the installed headers in inst/include/ and the registered C-callables.
#
# CPU kernels are architecture-generic: GGML_CPU_GENERIC forces the portable
# scalar/C reference path (see ggml-cpu/arch-fallback.h) - no -march SIMD
# flags, no OpenMP, no x86 hand-written SIMD kernel files, no GPU backends -
# so this compiles the same way regardless of the build machine's CPU.
# EXCEPTION: dense products use the BLAS backend (GGML_USE_BLAS, ggml-blas.cpp),
# offloading F32 mul_mat to R's own BLAS via the cblas->Fortran shim below.
# Runtime-SIMD-dispatched quantized kernels (GGML_CPU_ALL_VARIANTS) are the
# next planned addition for the dequant-dot path (see README).

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
DEFS = -DGGML_USE_CPU -DGGML_CPU_GENERIC -DGGML_USE_BLAS -DGGML_R_PACKAGE -D_GNU_SOURCE -DNDEBUG \
       -DGGML_VERSION='"0.9.5"' -DGGML_COMMIT='"vendored-unknown"' \
       -include rggml_compat.h

# Vulkan backend: empty unless ../../configure was run with --with-vulkan, in
# which case it passes the objects (ggml-vulkan.o plus one TU per generated
# SPIR-V shader) and the flags. -DGGML_USE_VULKAN must reach every TU, not just
# the Vulkan ones: ggml-backend-reg.cpp registers the backend behind that define.
VULKAN_OBJECTS ?=
VULKAN_CPPFLAGS ?=

ALL_CFLAGS   = $(CPPFLAGS) $(INCLUDES) $(DEFS) $(VULKAN_CPPFLAGS) $(CFLAGS)
ALL_CXXFLAGS = $(CPPFLAGS) $(INCLUDES) $(DEFS) $(VULKAN_CPPFLAGS) $(CXXFLAGS) -std=gnu++17

C_SOURCES = \
  ggml-private.c ggml-alloc.c \
  ggml-quants-legacy.c ggml-quants-iq.c ggml-quants-new.c ggml-quants-validate.c \
  ggml-cpu/ggml-cpu-backend.c ggml-cpu/quants.c

CXX_SOURCES = \
  ggml-backend.cpp ggml-backend-reg.cpp ggml-backend-meta.cpp ggml-threading.cpp \
  ggml-cpu/ggml-cpu.cpp ggml-cpu/traits.cpp ggml-cpu/vec.cpp ggml-cpu/repack.cpp \
  ggml-cpu/binary-ops.cpp ggml-cpu/unary-ops.cpp ggml-cpu/ops-private.cpp \
  ggml-blas.cpp

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

OBJECTS = $(C_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o) rggml_io.o rggml_cblas.o \
          cpu_features.o rggml_simd_dispatch.o $(SIMD_EXTRA_OBJECTS) \
          $(VULKAN_OBJECTS)

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

# rggml_cblas.c implements cblas_sgemm() over Fortran sgemm_. It needs R's
# headers (F77_NAME from <R_ext/RS.h>) but must NOT get the rggml_compat.h
# redirections, so it is built with a plain flag set (INCLUDES gives it cblas.h).
rggml_cblas.o: rggml_cblas.c
	$(CC) $(CPPFLAGS) $(INCLUDES) $(R_INCLUDE_FLAGS) $(CFLAGS) -c $< -o $@

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

clean:
	rm -f $(STATICLIB) $(OBJECTS)
