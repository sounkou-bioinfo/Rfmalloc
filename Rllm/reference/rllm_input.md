# Build a typed model program with ordinary R functions and pipes

These functions form the R-facing architecture language. A program is
traced from ordinary R calls, including functions returned by
`rllm_module()`, residual branches and structured loops. Calling
`rllm_program()` freezes the trace as data only: nodes, tensor
parameters, shapes, module paths and named outputs. The resulting object
contains no R closures, environments or backend pointers.

## Usage

``` r
rllm_input(name, shape, dtype = "f32")

rllm_parameter(name, shape, dtype = NULL, role = NULL)

rllm_module(name, forward)

rllm_op(x, op, ..., output_shape = NULL, output_dtype = NULL)

rllm_program(x, name = NULL)
```

## Arguments

- name:

  A non-empty input, parameter, module, loop or program name.

- shape:

  An atomic vector describing axes. Dimensions may be positive numbers
  or symbolic character strings.

- dtype:

  Storage or value type.

- role:

  Optional semantic role for a parameter.

- forward:

  An ordinary R function whose first argument is a traced value. Its
  body may use base R's `|>` pipe.

- x:

  A traced `rllm_value`, a named list of traced values for a multi-input
  operator, a model plan, a loaded model, or a GGUF path.

- op:

  A non-empty semantic operator name.

- ...:

  Named, data-only operator attributes, or arguments passed to a
  module's `forward` function.

- output_shape:

  Optional result shape. The input shape is retained when omitted.

- output_dtype:

  Optional result type. The input type is retained when omitted.

## Value

`rllm_input()` and operators return a traced `rllm_value`.
`rllm_parameter()` returns a data-only tensor reference, `rllm_module()`
returns a callable R function, and `rllm_program()` returns a data-only
`rllm_program`.

## Details

This is an architecture language, not a second tensor runtime. Operators
describe semantics; a backend may lower the operators it implements and
must reject the rest explicitly.
