// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDActiveView.h"

#include <unordered_set>

namespace Chaos
{
// This is an invertible spring class, typical springs are not invertible aware
template<class T, int d>
class PBDCollisionSpringConstraintsBase
{
  public:
	PBDCollisionSpringConstraintsBase(const TPBDActiveView<TPBDParticles<T, d>>& ParticlesActiveView, const TArray<TVector<int32, 3>>& Elements, const TSet<TVector<int32, 2>>& DisabledCollisionElements, const TArray<uint32>& DynamicGroupIds, const TArray<T>& PerGroupThicknesses, const T Dt, const T Stiffness = (T)1);
	virtual ~PBDCollisionSpringConstraintsBase() {}

	TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const int32 i) const;

  protected:
	TArray<TVector<int32, 4>> MConstraints;
	TArray<TVector<T, 3>> MBarys;

  private:
	const TArray<uint32>& MDynamicGroupIds;
	const TArray<T>& MPerGroupThicknesses;
	TArray<TVector<T, d>> MNormals; // per constraint, sign changes depending on orientation of colliding particle
	T MStiffness;
};
}
#endif
