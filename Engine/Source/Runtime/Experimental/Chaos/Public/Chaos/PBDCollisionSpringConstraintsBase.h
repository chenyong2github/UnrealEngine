// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Core.h"
#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Containers/Set.h"

namespace Chaos
{
// This is an invertible spring class, typical springs are not invertible aware
class CHAOS_API FPBDCollisionSpringConstraintsBase
{
public:
	FPBDCollisionSpringConstraintsBase(
		const int32 InOffset,
		const int32 InNumParticles,
		const TArray<TVec3<int32>>& InElements,
		TSet<TVec2<int32>>&& InDisabledCollisionElements,
		const FReal InThickness = (FReal)1.,
		const FReal InStiffness = (FReal)1.);

	virtual ~FPBDCollisionSpringConstraintsBase() {}

	void Init(const FPBDParticles& Particles);

	FVec3 GetDelta(const FPBDParticles& InParticles, const int32 i) const;

	const TArray<TVec4<int32>>& GetConstraints() const { return Constraints;  }
	const TArray<FVec3>& GetBarys() const { return Barys; }
	const TArray<FVec3>& GetNormals() const { return Normals; }
	FReal GetThickness() const { return Thickness; }

	void SetThickness(FReal InThickness) { Thickness = FMath::Max(InThickness, (FReal)0.);  }

protected:
	TArray<TVec4<int32>> Constraints;
	TArray<FVec3> Barys;
	TArray<FVec3> Normals;

private:
	const TArray<TVec3<int32>>& Elements;
	const TSet<TVec2<int32>> DisabledCollisionElements;  // TODO: Make this a bitarray
	int32 Offset;
	int32 NumParticles;
	FReal Thickness;
	FReal Stiffness;
};
}
#endif
