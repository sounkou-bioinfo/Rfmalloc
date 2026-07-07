# Makefile for the vendored GGML CPU backend.
#
# Invoked by ../../configure (which exports CC/CXX/AR/RANLIB/CFLAGS/CXXFLAGS
# using R's own toolchain settings, mirroring inst/fmalloc/Makefile in the
# Rfmalloc package). Produces libggml.a, which src/Makevars statically links
# into Rggml.so. Downstream packages never see this Makefile: they consume
# the installed headers in inst/include/ and the registered C-callables.
#
# This is a CPU-only, architecture-generic build: no SIMD compiler flags, no
# OpenMP, no Vulkan/CUDA/Metal/etc backends, no x86 hand-written SIMD kernel
# files. GGML_CPU_GENERIC forces the portable scalar/C reference kernels
# (see ggml-cpu/arch-fallback.h) so this compiles the same way regardless of
# the build machine's CPU - the right default for a CRAN-facing package.
# A faster, arch-specific build is a possible future addition (see README).

AR     ?= ar
RANLIB ?= ranlib
CC     ?= cc
CXX    ?= c++

STATICLIB = libggml.a

INCLUDES = -I. -Iggml-cpu -I../include

# -include rggml_compat.h redirects stdio/stderr/abort/exit in the vendored
# GGML sources to R-safe equivalents implemented in rggml_io.c (CRAN policy:
# no direct console writes, no process-terminating calls from library code).
DEFS = -DGGML_USE_CPU -DGGML_CPU_GENERIC -DGGML_R_PACKAGE -D_GNU_SOURCE \
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
  ggml-cpu/binary-ops.cpp ggml-cpu/unary-ops.cpp ggml-cpu/ops-private.cpp

OBJECTS = $(C_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o) rggml_io.o

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

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

clean:
	rm -f $(STATICLIB) $(OBJECTS)
