# Trace a structured, shared recurrence

Unlike an R `for` loop, `rllm_loop()` does not unroll its body. The body
is traced once into a nested data-only program and retained as a loop
node. A single value supports pipe syntax. A named list supports
multiple carried values, as required by recurrent answer and latent
states.

## Usage

``` r
rllm_loop(x, times, body, name = "loop")
```

## Arguments

- x:

  A traced value or named list of values from one program.

- times:

  A positive integer or one symbolic count.

- body:

  A function returning the same value structure as `x`.

- name:

  Loop name.

## Value

A traced value, or a named list of traced values when `x` is a list.
