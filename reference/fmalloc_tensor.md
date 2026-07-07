# Typed fmalloc tensors

A typed fmalloc tensor is an fmalloc raw vector holding a matrix payload
in a foreign element encoding (`"f32"`, `"f16"`, `"bf16"`, or any codec
registered by another package through the
`Rfmalloc_register_tensor_codec` C-callable), plus dimension and dtype
tags. Matrix products against dense double operands decode the payload
in bounded, block-aligned column panels that are streamed through BLAS
`dgemm`, so the double representation of the full tensor is never
materialized at once.

## Usage

``` r
fmalloc_tensor_codecs()

create_fmalloc_tensor(payload, dtype, dim)

as_fmalloc_tensor(x, dtype = "alp", runtime = NULL)

fmalloc_tensor_dtype(x)

fmalloc_tensor_materialize(x)

# S3 method for class 'fmalloc_tensor'
dim(x)

# S3 method for class 'fmalloc_tensor'
print(x, ...)

# S3 method for class 'fmalloc_tensor'
x %*% y

# S3 method for class 'fmalloc_tensor'
matrixOps(x, y)

# S3 method for class 'fmalloc_tensor'
crossprod(x, y = NULL, ...)

# S3 method for class 'fmalloc_tensor'
tcrossprod(x, y = NULL, ...)
```

## Arguments

- payload:

  An fmalloc raw vector holding the encoded payload in column-major
  order (first dimension fastest).

- dtype:

  Codec name, e.g. `"f32"`, `"f16"`, `"bf16"`; for
  `as_fmalloc_tensor()`, `"alp"` or `"sparse"`.

- dim:

  Integer dimensions of the decoded tensor (any rank). Storage and
  `fmalloc_tensor_materialize()` handle any number of dimensions; the
  matrix products (`%*%`,
  [`crossprod()`](https://rdrr.io/r/base/crossprod.html),
  [`tcrossprod()`](https://rdrr.io/r/base/crossprod.html)) require
  exactly 2.

- x:

  An `fmalloc_tensor` object (or, in `%*%`, a dense operand).

- runtime:

  Optional runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md).

- ...:

  Unused.

- y:

  The other matrix product operand.

## Value

`create_fmalloc_tensor()` returns an `fmalloc_tensor`.
`fmalloc_tensor_materialize()` and the matrix products return
fmalloc-backed double matrices. `fmalloc_tensor_codecs()` returns a
character vector.

## Details

`create_fmalloc_tensor()` tags an existing fmalloc raw payload.
`as_fmalloc_tensor()` compresses a double vector/matrix into fmalloc
storage with `dtype = "sparse"` (stores only the nonzeros of each chunk,
for mostly-zero data such as single-cell counts) or the builtin,
lossless `"alp"` codec (Afroozeh et al.,
[doi:10.1145/3626717](https://doi.org/10.1145/3626717) ; scalar core
adapted from the MIT-licensed zap implementation, see
`inst/COPYRIGHTS`), storing decimal-scaled doubles as bit-packed
integers in independently decodable 1024-value chunks with exact-value
patches and a raw escape hatch for incompressible chunks.
`fmalloc_tensor_materialize()` decodes the whole tensor into an fmalloc
double matrix. `fmalloc_tensor_codecs()` lists registered codec names,
and `fmalloc_tensor_dtype()` returns a tensor's dtype tag.

When a tensor's compressed payload reaches
`getOption("Rfmalloc.ooc_threshold_gb")`, its matrix products stream
out-of-core: each column panel's source pages are released after
decoding (for fixed-geometry codecs), so a tensor whose decoded `f64`
form exceeds RAM multiplies with a bounded resident set.
