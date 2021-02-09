// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Defines.h"
#include "Chaos/Matrix.h"
#include "Chaos/Rotation.h"
#include "Chaos/Vector.h"
#include "Containers/ArrayView.h"

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
namespace Chaos
{
	class FTriangleMesh;

	template<class T, int d>
	class TParticles;

	struct CHAOS_API FMassProperties
	{
		FMassProperties()
		    : Mass(0)
			, Volume(0)
		    , CenterOfMass(0)
		    , RotationOfMass(FRotation3::FromElements(FVec3(0), 1))
		    , InertiaTensor(0)
		{}
		FReal Mass;
		FReal Volume;
		FVec3 CenterOfMass;
		FRotation3 RotationOfMass;
		FMatrix33 InertiaTensor;
	};

	FRotation3 CHAOS_API TransformToLocalSpace(FMatrix33& Inertia);

	template<typename TSurfaces>
	void CHAOS_API CalculateVolumeAndCenterOfMass(const TParticles<FReal, 3>& Vertices, const TSurfaces& Surfaces, FReal& OutVolume, FVec3& OutCenterOfMass);

	template<typename TSurfaces>
	FMassProperties CHAOS_API CalculateMassProperties(const TParticles<FReal, 3>& Vertices, const TSurfaces& Surfaces, const FReal Mass);

	template<typename TSurfaces>
	void CHAOS_API CalculateInertiaAndRotationOfMass(const TParticles<FReal, 3>& Vertices, const TSurfaces& Surfaces, const FReal Density, const FVec3& CenterOfMass,
	    FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass);

	FMassProperties CHAOS_API Combine(const TArray<FMassProperties>& MPArray);

	FMassProperties CHAOS_API CombineWorldSpace(const TArray<FMassProperties>& MPArray, FReal InDensityKGPerCM);

}
#endif
