// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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
	using FRigidTransform3 = TRigidTransform<FReal, 3>;

	using FAABB3 = TAABB<FReal, 3>;

	template <typename T>
	using TVec2 = TVector<T, 2>;

	template <typename T>
	using TVec3 = TVector<T, 3>;

	template <typename T>
	using TVec4 = TVector<T, 4>;

	/** Core version string for Chaos data. Any DDC builder dependent on Chaos for serialization should depend on this version */
	static const FString ChaosVersionString = TEXT("BE819b6A-19B6-4A89-8120-9C995924D41D");
}
