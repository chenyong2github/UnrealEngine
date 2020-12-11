// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Containers/Set.h"

namespace Chaos
{
// This is an invertible spring class, typical springs are not invertible aware
template<class T, int d>
class TPBDCollisionSpringConstraintsBase
{
public:
	TPBDCollisionSpringConstraintsBase(
		const int32 InOffset,
		const int32 InNumParticles,
		const TArray<TVector<int32, 3>>& InElements,
		TSet<TVector<int32, 2>>&& InDisabledCollisionElements,
		const T InThickness = (T)1.,
		const T InStiffness = (T)1.);

	virtual ~TPBDCollisionSpringConstraintsBase() {}

	void Init(const TPBDParticles<T, d>& Particles);

	TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const int32 i) const;

	const TArray<TVector<int32, 4>>& GetConstraints() const { return Constraints;  }
	const TArray<TVector<T, d>>& GetBarys() const { return Barys; }
	const TArray<TVector<T, d>>& GetNormals() const { return Normals; }
	float GetThickness() const { return Thickness; }

protected:
	TArray<TVector<int32, 4>> Constraints;
	TArray<TVector<T, d>> Barys;
	TArray<TVector<T, d>> Normals;

private:
	const TArray<TVector<int32, 3>>& Elements;
	const TSet<TVector<int32, 2>> DisabledCollisionElements;  // TODO: Make this a bitarray
	int32 Offset;
	int32 NumParticles;
	T Thickness;
	T Stiffness;
};
}
#endif
