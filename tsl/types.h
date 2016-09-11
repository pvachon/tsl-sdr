#pragma once

#if TSL_POINTER_SIZE == 8
typedef uint32_t counter_t;
#elif TSL_POINTER_SIZE == 4
typedef uint64_t counter_t;
#else
#error Unknown pointer size specified, aborting.
#endif

