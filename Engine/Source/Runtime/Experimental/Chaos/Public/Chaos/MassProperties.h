// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Defines.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMesh.h"
#include "Containers/ArrayView.h"

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
namespace Chaos
{
	template<class T, int M, int N>
	class PMatrix;
	template<class T, int D>
	class TRotation;
	template<class T, int D>
	class TVector;

	template<class T, int d>
	struct TMassProperties
	{
		TMassProperties()
		    : Volume(0)
		    , CenterOfMass(0)
		    , RotationOfMass(TVector<T, d>(0), 1)
		    , InertiaTensor(0)
		{}
		T Volume;
		TVector<T, d> CenterOfMass;
		TRotation<T, d> RotationOfMass;
		PMatrix<T, d, d> InertiaTensor;
	};

	template<class T, int d>
	TRotation<T, d> TransformToLocalSpace(PMatrix<T, d, d>& Inertia);

	template<typename T, int d>
	void CalculateVolumeAndCenterOfMass(const TParticles<T, d>& Vertices, const TTriangleMesh<T>& Surface, T& OutVolume, TVector<T, d>& OutCenterOfMass);

	template<class T, int d>
	TMassProperties<T, d> CalculateMassProperties(
	    const TParticles<T, d>& Vertices,
	    const TTriangleMesh<T>& Surface,
	    const T Mass);

	template<typename T, int d>
	void CalculateInertiaAndRotationOfMass(const TParticles<T, d>& Vertices, const TTriangleMesh<T>& Surface, const T Density, const TVector<T, d>& CenterOfMass,
	    PMatrix<T, d, d>& OutInertiaTensor, TRotation<T, d>& OutRotationOfMass);

	template<class T, int d>
	TMassProperties<T, d> Combine(const TArray<TMassProperties<T, d>>& MPArray);

}
#endif
