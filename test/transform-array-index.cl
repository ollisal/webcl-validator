// RUN: cat %s | grep -v DRIVER-MAY-REJECT | %opencl-validator
// RUN: %webcl-validator %s -- -x cl -include %include/_kernel.h | grep -v CHECK | %FileCheck %s

__kernel void transform_array_index(
    // CHECK: __global int *array, unsigned long wcl_array_size)
    __global int *array)
{
    const int i = get_global_id(0);

    int pair[2] = { 0, 0 };
    // CHECK: pair[wcl_idx(i + 0, 2UL)] = 0;
    pair[i + 0] = 0;
    // CHECK: pair[wcl_idx(i + 1, 2UL)] = 1;
    (i + 1)[pair] = 1; // DRIVER-MAY-REJECT

    // CHECK: array[wcl_idx(i, wcl_array_size)] = pair[wcl_idx(i + 0, 2UL)]
    array[i] = pair[i + 0]
    // CHECK: + pair[wcl_idx(i + 1, 2UL)]
        + (i + 1)[pair] // DRIVER-MAY-REJECT
        ;
}
