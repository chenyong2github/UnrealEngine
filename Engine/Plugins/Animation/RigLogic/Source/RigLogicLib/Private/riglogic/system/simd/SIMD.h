#pragma once

#ifdef RL_USE_HALF_FLOATS
    #define TRIMD_ENABLE_F16C
#endif  // RL_USE_HALF_FLOATS

#ifdef RL_BUILD_WITH_AVX
    #define TRIMD_ENABLE_AVX
#endif  // RL_BUILD_WITH_AVX

#ifdef RL_BUILD_WITH_SSE
    #define TRIMD_ENABLE_SSE
#endif  // RL_BUILD_WITH_SSE

#include <trimd/TRiMD.h>
