#include <R.h>
#include <Rinternals.h>
#include <R_ext/Altrep.h>
#include <R_ext/Rallocators.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <stdexcept>

// Include fmalloc headers (C++ interface)
#include "fmalloc.hpp"

// Global fmalloc info structure
static struct fm_info *global_fm_info = nullptr;

//==============================================================================
// ALTREP class definition for memory-mapped vectors
//==============================================================================

static R_altrep_class_t mmap_integer_class;

// Structure to hold mmap information
typedef struct {
    void *data;
    size_t length;
    int fd;
    char *filepath;
} mmap_info_t;

// ALTREP method implementations
static SEXP mmap_duplicate(SEXP x, Rboolean deep) {
    return x; // For simplicity, return same object
}

static void mmap_finalize(SEXP x) {
    SEXP data1 = R_altrep_data1(x);
    if (data1 == R_NilValue || TYPEOF(data1) != EXTPTRSXP) {
        return; // Nothing to finalize
    }
    
    mmap_info_t *info = (mmap_info_t*) R_ExternalPtrAddr(data1);
    if (info) {
        if (info->data && info->data != MAP_FAILED) {
            munmap(info->data, info->length * sizeof(int));
        }
        if (info->fd >= 0) {
            close(info->fd);
        }
        if (info->filepath) {
            free(info->filepath);
        }
        free(info);
    }
}

static Rboolean mmap_inspect(SEXP x, int pre, int deep, int pvec,
                            void (*inspect_subtree)(SEXP, int, int, int)) {
    Rprintf("ALTREP mmap integer vector\n");
    return TRUE;
}

static R_xlen_t mmap_length(SEXP x) {
    SEXP data1 = R_altrep_data1(x);
    if (data1 == R_NilValue || TYPEOF(data1) != EXTPTRSXP) {
        return 0;
    }
    
    mmap_info_t *info = (mmap_info_t*) R_ExternalPtrAddr(data1);
    return info ? info->length : 0;
}

static void *mmap_dataptr(SEXP x, Rboolean writeable) {
    SEXP data1 = R_altrep_data1(x);
    if (data1 == R_NilValue || TYPEOF(data1) != EXTPTRSXP) {
        return nullptr;
    }
    
    mmap_info_t *info = (mmap_info_t*) R_ExternalPtrAddr(data1);
    return info ? info->data : nullptr;
}

static const void *mmap_dataptr_or_null(SEXP x) {
    return mmap_dataptr(x, FALSE);
}

static int mmap_elt(SEXP x, R_xlen_t i) {
    SEXP data1 = R_altrep_data1(x);
    if (data1 == R_NilValue || TYPEOF(data1) != EXTPTRSXP) {
        return NA_INTEGER;
    }
    
    mmap_info_t *info = (mmap_info_t*) R_ExternalPtrAddr(data1);
    if (!info || !info->data || i >= (R_xlen_t)info->length) {
        return NA_INTEGER;
    }
    return ((int*) info->data)[i];
}

static R_xlen_t mmap_get_region(SEXP x, R_xlen_t start, R_xlen_t size, int *buf) {
    SEXP data1 = R_altrep_data1(x);
    if (data1 == R_NilValue || TYPEOF(data1) != EXTPTRSXP) {
        return 0;
    }
    
    mmap_info_t *info = (mmap_info_t*) R_ExternalPtrAddr(data1);
    if (!info || !info->data) {
        return 0;
    }
    
    R_xlen_t end = start + size;
    if (end > (R_xlen_t)info->length) {
        end = (R_xlen_t)info->length;
    }
    
    R_xlen_t actual_size = end - start;
    if (actual_size > 0) {
        memcpy(buf, ((int*) info->data) + start, actual_size * sizeof(int));
    }
    
    return actual_size;
}

//==============================================================================
// fmalloc allocator functions
//==============================================================================

static void *fmalloc_r_alloc(R_allocator_t *allocator, size_t length) {
    if (!global_fm_info) {
        Rf_error("fmalloc not initialized");
        return nullptr;
    }
    
    if (length == 0) {
        return nullptr; // R handles zero-length allocations
    }
    
    try {
        fmalloc_set_target(global_fm_info);
        void *mem = fmalloc(length);
        
        if (mem) {
            Rprintf("fmalloc allocated %zu bytes at %p\n", length, mem);
        } else {
            Rf_warning("fmalloc failed to allocate %zu bytes", length);
        }
        
        return mem;
    } catch (const std::exception& e) {
        Rf_error("fmalloc allocation failed: %s", e.what());
        return nullptr;
    } catch (...) {
        Rf_error("fmalloc allocation failed: unknown error");
        return nullptr;
    }
}

static void fmalloc_r_free(R_allocator_t *allocator, void *mem) {
    if (!global_fm_info) {
        Rf_warning("fmalloc not initialized during free");
        return;
    }
    
    if (!mem) {
        return; // Nothing to free
    }
    
    try {
        fmalloc_set_target(global_fm_info);
        Rprintf("fmalloc freed %p\n", mem);
        ffree(mem);
    } catch (const std::exception& e) {
        Rf_warning("fmalloc free failed: %s", e.what());
    } catch (...) {
        Rf_warning("fmalloc free failed: unknown error");
    }
}

//==============================================================================
// R interface functions
//==============================================================================

extern "C" {

// Create mmap-backed ALTREP vector
SEXP create_mmap_vector_impl(SEXP filepath_sexp, SEXP length_sexp) {
    // Input validation
    if (TYPEOF(filepath_sexp) != STRSXP || LENGTH(filepath_sexp) != 1) {
        Rf_error("filepath must be a single character string");
        return R_NilValue;
    }
    
    if (TYPEOF(length_sexp) != INTSXP || LENGTH(length_sexp) != 1) {
        Rf_error("length must be a single integer");
        return R_NilValue;
    }
    
    const char *filepath = CHAR(STRING_ELT(filepath_sexp, 0));
    if (!filepath || strlen(filepath) == 0) {
        Rf_error("filepath cannot be empty");
        return R_NilValue;
    }
    
    R_xlen_t length = asInteger(length_sexp);
    if (length <= 0) {
        Rf_error("length must be a positive integer");
        return R_NilValue;
    }
    
    // Check for integer overflow
    size_t file_size = length * sizeof(int);
    if (file_size / sizeof(int) != (size_t)length) {
        Rf_error("length too large, would cause integer overflow");
        return R_NilValue;
    }
    
    // Open/create file
    int fd = open(filepath, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        Rf_error("Cannot open file: %s (errno: %d)", filepath, errno);
        return R_NilValue;
    }
    
    // Ensure file has the right size
    if (ftruncate(fd, file_size) != 0) {
        close(fd);
        Rf_error("Cannot set file size to %zu bytes (errno: %d)", file_size, errno);
        return R_NilValue;
    }
    
    // Memory map the file
    void *data = mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        Rf_error("Memory mapping failed for file %s, size %zu (errno: %d)", filepath, file_size, errno);
        return R_NilValue;
    }
    
    // Create info structure
    mmap_info_t *info = (mmap_info_t*) malloc(sizeof(mmap_info_t));
    if (!info) {
        munmap(data, file_size);
        close(fd);
        Rf_error("Failed to allocate memory for mmap info structure");
        return R_NilValue;
    }
    
    info->data = data;
    info->length = length;
    info->fd = fd;
    info->filepath = strdup(filepath);
    if (!info->filepath) {
        free(info);
        munmap(data, file_size);
        close(fd);
        Rf_error("Failed to allocate memory for filepath copy");
        return R_NilValue;
    }
    
    // Create external pointer to hold the info
    SEXP info_ptr = PROTECT(R_MakeExternalPtr(info, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(info_ptr, mmap_finalize, TRUE);
    
    // Create ALTREP object
    SEXP result = R_new_altrep(mmap_integer_class, info_ptr, R_NilValue);
    
    UNPROTECT(1);
    return result;
}

// Initialize fmalloc
SEXP init_fmalloc_impl(SEXP filepath_sexp) {
    if (global_fm_info) {
        Rf_warning("fmalloc already initialized");
        return ScalarLogical(FALSE);
    }
    
    if (TYPEOF(filepath_sexp) != STRSXP || LENGTH(filepath_sexp) != 1) {
        Rf_error("filepath must be a single character string");
        return R_NilValue;
    }
    
    const char *filepath = CHAR(STRING_ELT(filepath_sexp, 0));
    if (!filepath || strlen(filepath) == 0) {
        Rf_error("filepath cannot be empty");
        return R_NilValue;
    }
    
    bool init_flag = false;
    
    // Wrap the fmalloc_init call in a try-catch to handle C++ exceptions
    try {
        global_fm_info = fmalloc_init(filepath, &init_flag);
    } catch (const std::exception& e) {
        Rf_error("fmalloc initialization failed with exception: %s", e.what());
        return R_NilValue;
    } catch (...) {
        Rf_error("fmalloc initialization failed with unknown exception");
        return R_NilValue;
    }
    
    if (!global_fm_info) {
        Rf_error("Failed to initialize fmalloc with file: %s", filepath);
        return R_NilValue;
    }
    
    Rprintf("fmalloc initialized with file: %s (init: %s)\n", 
            filepath, init_flag ? "true" : "false");
    
    return ScalarLogical(init_flag);
}

// Create vector using fmalloc
SEXP create_fmalloc_vector_impl(SEXP template_vec, SEXP length_sexp) {
    if (!global_fm_info) {
        Rf_error("fmalloc not initialized. Call init_fmalloc() first.");
        return R_NilValue;
    }
    
    if (!template_vec || TYPEOF(length_sexp) != INTSXP) {
        Rf_error("Invalid arguments to create_fmalloc_vector");
        return R_NilValue;
    }
    
    int length = asInteger(length_sexp);
    if (length < 0) {
        Rf_error("length must be a positive integer");
        return R_NilValue;
    }
    
    if (length == 0) {
        // Return empty vector of the same type
        return allocVector(TYPEOF(template_vec), 0);
    }
    
    // Validate vector type
    int vec_type = TYPEOF(template_vec);
    if (vec_type != INTSXP && vec_type != REALSXP && vec_type != LGLSXP) {
        Rf_error("Unsupported vector type: %d", vec_type);
        return R_NilValue;
    }
    
    try {
        R_allocator_t allocator = { fmalloc_r_alloc, fmalloc_r_free, nullptr, nullptr };
        SEXP result = Rf_allocVector3(vec_type, length, &allocator);
        return result;
    } catch (const std::exception& e) {
        Rf_error("Failed to create fmalloc vector: %s", e.what());
        return R_NilValue;
    } catch (...) {
        Rf_error("Failed to create fmalloc vector: unknown error");
        return R_NilValue;
    }
}

// Cleanup fmalloc
SEXP cleanup_fmalloc_impl() {
    if (global_fm_info) {
        try {
            // Note: We don't call any cleanup functions on the fmalloc library
            // as it manages its own memory mapping. We just reset our pointer.
            global_fm_info = nullptr;
            Rprintf("fmalloc cleaned up\n");
        } catch (const std::exception& e) {
            Rf_warning("Error during fmalloc cleanup: %s", e.what());
        } catch (...) {
            Rf_warning("Unknown error during fmalloc cleanup");
        }
    }
    return R_NilValue;
}

// Registration array
static const R_CallMethodDef CallEntries[] = {
    {"create_mmap_vector_impl", (DL_FUNC) &create_mmap_vector_impl, 2},
    {"init_fmalloc_impl", (DL_FUNC) &init_fmalloc_impl, 1},
    {"create_fmalloc_vector_impl", (DL_FUNC) &create_fmalloc_vector_impl, 2},
    {"cleanup_fmalloc_impl", (DL_FUNC) &cleanup_fmalloc_impl, 0},
    {nullptr, nullptr, 0}
};

// Initialize the ALTREP class
void init_mmap_altrep_class(DllInfo *dll) {
    mmap_integer_class = R_make_altinteger_class("mmap_integer", "fmalloc", dll);
    
    // Set methods
    R_set_altrep_Duplicate_method(mmap_integer_class, mmap_duplicate);
    R_set_altrep_Inspect_method(mmap_integer_class, mmap_inspect);
    R_set_altrep_Length_method(mmap_integer_class, mmap_length);
    
    // Set integer-specific methods
    R_set_altinteger_Elt_method(mmap_integer_class, mmap_elt);
    R_set_altinteger_Get_region_method(mmap_integer_class, mmap_get_region);
    
    // Set data access methods  
    R_set_altvec_Dataptr_method(mmap_integer_class, mmap_dataptr);
    R_set_altvec_Dataptr_or_null_method(mmap_integer_class, mmap_dataptr_or_null);
}

// Package initialization
void R_init_Rfmalloc(DllInfo *dll) {
    R_registerRoutines(dll, nullptr, CallEntries, nullptr, nullptr);
    R_useDynamicSymbols(dll, FALSE);
    init_mmap_altrep_class(dll);
}

} // extern "C"
