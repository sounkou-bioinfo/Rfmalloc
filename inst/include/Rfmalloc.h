#ifndef RFMALLOC_API_H
#define RFMALLOC_API_H

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rfmalloc C-callable API, version 2.
 *
 * These functions are resolved with R_GetCCallable(). Packages should import
 * Rfmalloc at runtime before calling them, for example by listing Rfmalloc in
 * LinkingTo and Imports. Returned SEXP objects follow normal R API ownership:
 * protect them if allocating further R objects, and preserve them if they must
 * outlive the current call.
 */

typedef int (*Rfmalloc_api_version_fun)(void);
typedef SEXP (*Rfmalloc_default_runtime_fun)(void);
typedef SEXP (*Rfmalloc_open_runtime_fun)(const char *filepath, double size_gb,
                                          const char *mode);
typedef SEXP (*Rfmalloc_create_vector_fun)(SEXP runtime, int sexptype,
                                           R_xlen_t length);
typedef void (*Rfmalloc_cleanup_runtime_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_list_allocations_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_is_fmalloc_vector_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_vector_type_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_vector_length_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_vector_payload_ptr_fun)(SEXP vector);

static inline Rfmalloc_api_version_fun Rfmalloc_api_version_ptr(void)
{
    return (Rfmalloc_api_version_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_api_version");
}

static inline Rfmalloc_default_runtime_fun Rfmalloc_default_runtime_ptr(void)
{
    return (Rfmalloc_default_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_default_runtime");
}

static inline Rfmalloc_open_runtime_fun Rfmalloc_open_runtime_ptr(void)
{
    return (Rfmalloc_open_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_open_runtime");
}

static inline Rfmalloc_create_vector_fun Rfmalloc_create_vector_ptr(void)
{
    return (Rfmalloc_create_vector_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_create_vector");
}

static inline Rfmalloc_cleanup_runtime_fun Rfmalloc_cleanup_runtime_ptr(void)
{
    return (Rfmalloc_cleanup_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_cleanup_runtime");
}

static inline Rfmalloc_list_allocations_fun Rfmalloc_list_allocations_ptr(void)
{
    return (Rfmalloc_list_allocations_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_list_allocations");
}

static inline Rfmalloc_is_fmalloc_vector_fun Rfmalloc_is_fmalloc_vector_ptr(void)
{
    return (Rfmalloc_is_fmalloc_vector_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_is_fmalloc_vector");
}

static inline Rfmalloc_vector_type_fun Rfmalloc_vector_type_ptr(void)
{
    return (Rfmalloc_vector_type_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_type");
}

static inline Rfmalloc_vector_length_fun Rfmalloc_vector_length_ptr(void)
{
    return (Rfmalloc_vector_length_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_length");
}

static inline Rfmalloc_vector_payload_ptr_fun Rfmalloc_vector_payload_ptr_ptr(void)
{
    return (Rfmalloc_vector_payload_ptr_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_payload_ptr");
}

static inline int Rfmalloc_api_version(void)
{
    return Rfmalloc_api_version_ptr()();
}

static inline SEXP Rfmalloc_default_runtime(void)
{
    return Rfmalloc_default_runtime_ptr()();
}

static inline SEXP Rfmalloc_open_runtime(const char *filepath, double size_gb,
                                         const char *mode)
{
    return Rfmalloc_open_runtime_ptr()(filepath, size_gb, mode);
}

static inline SEXP Rfmalloc_create_vector(SEXP runtime, int sexptype,
                                          R_xlen_t length)
{
    return Rfmalloc_create_vector_ptr()(runtime, sexptype, length);
}

static inline void Rfmalloc_cleanup_runtime(SEXP runtime)
{
    Rfmalloc_cleanup_runtime_ptr()(runtime);
}

static inline SEXP Rfmalloc_list_allocations(SEXP runtime)
{
    return Rfmalloc_list_allocations_ptr()(runtime);
}

static inline SEXP Rfmalloc_is_fmalloc_vector(SEXP vector)
{
    return Rfmalloc_is_fmalloc_vector_ptr()(vector);
}

static inline SEXP Rfmalloc_vector_type(SEXP vector)
{
    return Rfmalloc_vector_type_ptr()(vector);
}

static inline SEXP Rfmalloc_vector_length(SEXP vector)
{
    return Rfmalloc_vector_length_ptr()(vector);
}

static inline SEXP Rfmalloc_vector_payload_ptr(SEXP vector)
{
    return Rfmalloc_vector_payload_ptr_ptr()(vector);
}

#ifdef __cplusplus
}
#endif

#endif /* RFMALLOC_API_H */
