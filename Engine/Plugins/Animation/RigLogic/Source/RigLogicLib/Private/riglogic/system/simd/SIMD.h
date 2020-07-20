#pragma once

#ifdef RL_BUILD_WITH_AVX
    #include "riglogic/system/simd/AVX.h"
#endif
#ifdef RL_BUILD_WITH_SSE
    #include "riglogic/system/simd/SSE.h"
#endif
#include "riglogic/system/simd/Scalar.h"
