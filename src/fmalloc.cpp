#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rallocators.h>
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
// fmalloc allocator functions with realloc support
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
            // Check errno for specific error
            const char *error_msg = "Unknown allocation failure";
            if (errno == ENOMEM) {
                error_msg = "Out of memory";
            }
            Rf_error("fmalloc failed to allocate %zu bytes: %s (errno: %d)", 
                     length, error_msg, errno);
            return nullptr;
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

static void fmalloc_r_free(R_allocator_t *allocator, void* ptr) {
    if (!ptr) {
        Rprintf("fmalloc_r_free called with NULL pointer\n");
        return; // R handles null pointer frees
    }
    
    if (!global_fm_info) {
        // If fmalloc is not initialized, don't try to free - let R handle it
        Rprintf("fmalloc_r_free called but fmalloc not initialized (ptr: %p)\n", ptr);
        return;
    }
    
    try {
        fmalloc_set_target(global_fm_info);
        Rprintf("fmalloc freed memory at %p\n", ptr);
        dlfree(ptr); // fmalloc's free function
    } catch (const std::exception& e) {
        // Don't error on free failures - just warn
        Rf_warning("fmalloc free failed for %p: %s", ptr, e.what());
    } catch (...) {
        // Don't error on free failures - just warn  
        Rf_warning("fmalloc free failed for %p: unknown error", ptr);
    }
}

extern "C" {

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
    
    // Check if file exists, create if it doesn't
    struct stat st;
    bool file_exists = (stat(filepath, &st) == 0);
    
    if (!file_exists) {
        // Create the file with a minimum size (32MB to be safe)
        // fmalloc requires at least FMALLOC_MIN_CHUNK (16MB) + 8KB overhead
        int fd = open(filepath, O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            Rf_error("Cannot create file: %s (errno: %d)", filepath, errno);
            return R_NilValue;
        }
        
        size_t initial_size = 32 * 1024 * 1024; // 32MB
        if (ftruncate(fd, initial_size) != 0) {
            close(fd);
            Rf_error("Cannot set initial file size for: %s (errno: %d)", filepath, errno);
            return R_NilValue;
        }
        close(fd);
    }
    
    bool init_flag = false;
    
    // Wrap the fmalloc_init call in a try-catch to handle C++ exceptions
    // Note: fmalloc_init() may call exit(1) on errors, which we cannot catch
    // We validate conditions beforehand to minimize this risk
    try {
        // Check file accessibility before calling fmalloc_init
        int test_fd = open(filepath, O_RDWR);
        if (test_fd < 0) {
            Rf_error("Cannot access file: %s (errno: %d)", filepath, errno);
            return R_NilValue;
        }
        
        struct stat test_st;
        if (fstat(test_fd, &test_st) < 0) {
            close(test_fd);
            Rf_error("Cannot stat file: %s (errno: %d)", filepath, errno);
            return R_NilValue;
        }
        close(test_fd);
        
        // Check minimum file size (fmalloc needs at least 16MB + overhead)
        if (test_st.st_size < (16 * 1024 * 1024 + 8192)) {
            Rf_error("File too small: %s (size: %lld, minimum: %lld)", 
                     filepath, (long long)test_st.st_size, (long long)(16 * 1024 * 1024 + 8192));
            return R_NilValue;
        }
        
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
        Rprintf("Creating fmalloc vector: type=%d, length=%d\n", vec_type, length);
        SEXP result = Rf_allocVector3(vec_type, length, &allocator);
        Rprintf("Successfully created fmalloc vector\n");
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
            Rprintf("Cleaning up fmalloc...\n");
            // Note: We don't call any cleanup functions on the fmalloc library
            // as it manages its own memory mapping. We just reset our pointer.
            global_fm_info = nullptr;
            Rprintf("fmalloc cleaned up\n");
        } catch (const std::exception& e) {
            Rf_warning("Error during fmalloc cleanup: %s", e.what());
        } catch (...) {
            Rf_warning("Unknown error during fmalloc cleanup");
        }
    } else {
        Rprintf("cleanup_fmalloc called but fmalloc not initialized\n");
    }
    return R_NilValue;
}

// Registration array
static const R_CallMethodDef CallEntries[] = {
    {"init_fmalloc_impl", (DL_FUNC) &init_fmalloc_impl, 1},
    {"create_fmalloc_vector_impl", (DL_FUNC) &create_fmalloc_vector_impl, 2},
    {"cleanup_fmalloc_impl", (DL_FUNC) &cleanup_fmalloc_impl, 0},
    {nullptr, nullptr, 0}
};

// Package initialization
void R_init_Rfmalloc(DllInfo *dll) {
    R_registerRoutines(dll, nullptr, CallEntries, nullptr, nullptr);
    R_useDynamicSymbols(dll, FALSE);
}

} // extern "C"
