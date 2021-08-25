// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Include the current implementation of a FTransform, depending on the vector processing mode
 */

#include "CoreTypes.h"
#include "Math/Quat.h"
#include "Math/ScalarRegister.h"
#include "Math/TransformVectorized.h"
#include "Math/TransformNonVectorized.h"

DECLARE_LWC_TYPE(Transform, 3);

template<> struct TIsPODType<FTransform3f> { enum { Value = true }; };
template<> struct TIsPODType<FTransform3d> { enum { Value = true }; };
template<> struct TIsUECoreType<FTransform3f> { enum { Value = true }; };
template<> struct TIsUECoreType<FTransform3d> { enum { Value = true }; };
template<> struct TCanBulkSerialize<FTransform3f> { enum { Value = false }; }; // LWC_TODO: this was never defined before, should it be true or false?
template<> struct TCanBulkSerialize<FTransform3d> { enum { Value = false }; }; // LWC_TODO: This can be done (via versioning) once LWC is fixed to on.
DECLARE_INTRINSIC_TYPE_LAYOUT(FTransform3f);
DECLARE_INTRINSIC_TYPE_LAYOUT(FTransform3d);

// ispc doesn't export typedefs in generated headers, so we do it here to keep our code happy.
DECLARE_LWC_TYPE_ISPC(Transform, 3);
