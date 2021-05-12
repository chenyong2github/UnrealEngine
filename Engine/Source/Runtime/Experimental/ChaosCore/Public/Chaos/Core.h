// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Real.h"
#include "Chaos/Vector.h"
#include "Chaos/Matrix.h"
#include "Chaos/Rotation.h"
#include "Chaos/Transform.h"
#include "Containers/UnrealString.h"

namespace Chaos
{
	template<class T, int d> class TAABB;

	using FVec2 = TVector<FReal, 2>;
	using FVec3 = TVector<FReal, 3>;
	using FVec4 = TVector<FReal, 4>;
	using FRotation3 = TRotation<FReal, 3>;
	using FMatrix33 = PMatrix<FReal, 3, 3>;
	using FMatrix44 = PMatrix<FReal, 4, 4>;
	using FRigidTransform3 = TRigidTransform<FReal, 3>;

	using FAABB3 = TAABB<FReal, 3>;

	template <typename T>
	using TVec2 = TVector<T, 2>;

	template <typename T>
	using TVec3 = TVector<T, 3>;

	template <typename T>
	using TVec4 = TVector<T, 4>;

	// NOTE: if you get a merge conflict on the GUID, you must replace it with a new GUID - do not accept the source or target
	// or you will likely get DDC version conflicts resulting in crashes during load.
	// Core version string for Chaos data. Any DDC builder dependent on Chaos for serialization should depend on this version
	static const FString ChaosVersionString = TEXT("3AD4DA9E-B101-41B6-A1D3-31AB4F02FBD6");
}
