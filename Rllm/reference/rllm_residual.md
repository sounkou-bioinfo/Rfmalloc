# Trace a residual branch

`branch` is evaluated immediately with `x`. Its nodes are recorded, then
an explicit add node joins the original value and the branch result.

## Usage

``` r
rllm_residual(x, branch)
```

## Arguments

- x:

  A traced `rllm_value`.

- branch:

  A function of one traced value.

## Value

A traced `rllm_value`.
