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

ALL_CFLAGS   = $(CPPFLAGS) $(INCLUDES) $(DEFS) $(CFLAGS)
ALL_CXXFLAGS = $(CPPFLAGS) $(INCLUDES) $(DEFS) $(CXXFLAGS) -std=gnu++17

C_SOURCES = \
  ggml-private.c ggml-alloc.c \
  ggml-quants-legacy.c ggml-quants-iq.c ggml-quants-new.c ggml-quants-validate.c \
  ggml-cpu/ggml-cpu-backend.c ggml-cpu/quants.c

CXX_SOURCES = \
  ggml-backend.cpp ggml-backend-reg.cpp ggml-backend-meta.cpp ggml-threading.cpp \
  ggml-cpu/ggml-cpu.cpp ggml-cpu/traits.cpp ggml-cpu/vec.cpp ggml-cpu/repack.cpp \
  ggml-cpu/binary-ops.cpp ggml-cpu/unary-ops.cpp ggml-cpu/ops-private.cpp \
  ggml-blas.cpp

OBJECTS = $(C_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o) rggml_io.o rggml_cblas.o

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

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

clean:
	rm -f $(STATICLIB) $(OBJECTS)
