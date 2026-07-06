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

  An fmalloc raw vector holding the encoded matrix payload in
  column-major order (first dimension fastest).

- dtype:

  Codec name, e.g. `"f32"`, `"f16"`, `"bf16"`.

- dim:

  Length-2 integer dimensions of the decoded matrix.

- x:

  An `fmalloc_tensor` object (or, in `%*%`, a dense operand).

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
`fmalloc_tensor_materialize()` decodes the whole tensor into an fmalloc
double matrix. `fmalloc_tensor_codecs()` lists registered codec names,
and `fmalloc_tensor_dtype()` returns a tensor's dtype tag.
