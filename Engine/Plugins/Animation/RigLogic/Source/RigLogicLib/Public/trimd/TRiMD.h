// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "trimd/AVX.h"
#include "trimd/SSE.h"
#include "trimd/Scalar.h"

namespace trimd {

#ifdef TRIMD_ENABLE_AVX
    using F256 = avx::F256;

    inline void transpose(F256& row0, F256& row1, F256& row2, F256& row3, F256& row4, F256& row5, F256& row6, F256& row7) {
        avx::transpose(row0, row1, row2, row3, row4, row5, row6, row7);
    }
#else
    using F256 = scalar::F256;

    inline void transpose(F256& row0, F256& row1, F256& row2, F256& row3, F256& row4, F256& row5, F256& row6, F256& row7) {
        scalar::transpose(row0, row1, row2, row3, row4, row5, row6, row7);
    }
#endif  // TRIMD_ENABLE_AVX

#ifdef TRIMD_ENABLE_SSE
    using F128 = sse::F128;

    inline void transpose(F128& row0, F128& row1, F128& row2, F128& row3) {
        sse::transpose(row0, row1, row2, row3);
    }
#else
    using F128 = scalar::F128;

    inline void transpose(F128& row0, F128& row1, F128& row2, F128& row3) {
        scalar::transpose(row0, row1, row2, row3);
    }
#endif  // TRIMD_ENABLE_SSE

}  // namespace trimd
