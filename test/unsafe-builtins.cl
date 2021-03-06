// RUN: %opencl-validator < %s
// RUN: %webcl-validator %s 2>&1 | grep -v CHECK | %FileCheck %s

__kernel void unsafe_builtins(
    float x,
    __constant float *input,
    __global float *output)
{
    const size_t i = get_global_id(0);

    __local int offset;
    offset = i;
    // CHECK: error: Builtin argument check is required.
    offset = atomic_add(&offset, 1);

    float x_floor;
    // CHECK: error: Builtin argument check is required.
    float x_fract = fract(x, &x_floor);
}
