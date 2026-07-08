#' Vulkan GPU backend availability
#'
#' Reports the Vulkan devices GGML can see. Rggml only contains the Vulkan
#' backend when it was **built with it** - it is opt-in, because generating and
#' compiling GGML's 156 embedded SPIR-V shaders is expensive (the largest one
#' needs several GB of RAM):
#'
#' \preformatted{
#'   install.packages("Rggml", configure.args = "--with-vulkan")
#'   R CMD INSTALL --configure-args=--with-vulkan .
#' }
#'
#' It also requires `glslc` and the Vulkan headers at build time
#' (`libvulkan-dev` + `glslc` on Debian/Ubuntu, or the LunarG Vulkan SDK with
#' `VULKAN_SDK` set), and a Vulkan driver at run time. A software driver such
#' as Mesa's lavapipe counts: it is slow, but it makes the backend testable
#' without a GPU.
#'
#' When Rggml was built without Vulkan, this returns zero devices rather than
#' failing, so callers can probe and fall back.
#'
#' @return A list with `n_devices` (integer) and `device` (the description of
#'   device 0, or `NA` when there is none).
#' @examples
#' rggml_vulkan_info()
#' @export
rggml_vulkan_info <- function() {
    .Call("RC_rggml_vulkan_info")
}

#' @rdname rggml_vulkan_info
#' @return `rggml_has_vulkan()` returns `TRUE` when at least one Vulkan device
#'   is usable.
#' @export
rggml_has_vulkan <- function() {
    rggml_vulkan_info()$n_devices > 0L
}
