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

	const TArray<TVector<int32, 4>>& GetConstraints() const { return MConstraints;  }
	const TArray<TVector<T, d>>& GetBarys() const { return MBarys; }
	const TArray<TVector<T, d>>& GetNormals() const { return MNormals; }
	float GetThickness() const { return MThickness; }

protected:
	TArray<TVector<int32, 4>> MConstraints;
	TArray<TVector<T, d>> MBarys;
	TArray<TVector<T, d>> MNormals;

private:
	const TArray<TVector<int32, 3>>& MElements;
	const TSet<TVector<int32, 2>> MDisabledCollisionElements;  // TODO: Make this a bitarray
	int32 MOffset;
	int32 MNumParticles;
	T MThickness;
	T MStiffness;
};
}
#endif
