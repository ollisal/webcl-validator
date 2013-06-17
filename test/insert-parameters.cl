// RUN: cat %s | %opencl-validator
// RUN: %webcl-validator %s -- -x cl -include %include/_kernel.h 2>&1 | grep -v CHECK | %FileCheck %s

// CHECK-NOT: WclAddressSpaces *wcl_as,
int no_parameters()
{
    return 0;
}

int value_parameters(
    // CHECK: WclAddressSpaces *wcl_as,
    // CHECK: int index)
    int index)
{
    return index + 1;
}

int unused_parameters(
    // CHECK: WclAddressSpaces *wcl_as,
    // CHECK: __global int *global_array, __local int *local_array,
    __global int *global_array, __local int *local_array,
    // CHECK: __constant int *constant_array, __private int *private_array)
    __constant int *constant_array, __private int *private_array)
{
    return 0;
}

int used_parameters(
    // CHECK: WclAddressSpaces *wcl_as,
    // CHECK: __global int *global_array, __local int *local_array,
    __global int *global_array, __local int *local_array,
    // CHECK: __constant int *constant_array, __private int *private_array)
    __constant int *constant_array, __private int *private_array)
{
    const int index = value_parameters(no_parameters());
    return global_array[index] + local_array[index] +
        constant_array[index] + private_array[index];
}

__kernel void insert_parameters(
    // CHECK-NOT: WclAddressSpaces *wcl_as,
    // CHECK: __global int *global_array, unsigned long wcl_global_array_size,
    __global int *global_array,
    // CHECK: __local int *local_array, unsigned long wcl_local_array_size,
    __local int *local_array,
    // CHECK: __constant int *constant_array, unsigned long wcl_constant_array_size)
    __constant int *constant_array)
{
    const int i = get_global_id(0);

    int private_array[2] = { 0 };

    global_array[i + 0] = unused_parameters(global_array, local_array,
                                            constant_array, private_array);
    global_array[i + 1] = used_parameters(global_array, local_array,
                                          constant_array, private_array);
}