# Vendored 'GGML' library version

Returns the version string reported by the vendored 'GGML' library at
runtime (`ggml_version()`), resolved through Rggml's own registered
C-callable rather than a compile-time constant, so it always reflects
the code that was actually built.

## Usage

``` r
ggml_version()
```

## Value

A length-1 character vector.

## Examples

``` r
ggml_version()
#> [1] "0.16.0"
```
