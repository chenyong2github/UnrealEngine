// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Core.h"
#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "Chaos/PBDParticles.h"
#include "Containers/Set.h"

namespace Chaos::Softs
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
		const FSolverReal InThickness = (FSolverReal)1.,
		const FSolverReal InStiffness = (FSolverReal)1.);

	virtual ~FPBDCollisionSpringConstraintsBase() {}

	void Init(const FSolverParticles& Particles);

	FSolverVec3 GetDelta(const FSolverParticles& InParticles, const int32 i) const;

	const TArray<TVec4<int32>>& GetConstraints() const { return Constraints;  }
	const TArray<FSolverVec3>& GetBarys() const { return Barys; }
	const TArray<FSolverVec3>& GetNormals() const { return Normals; }
	FSolverReal GetThickness() const { return Thickness; }

	void SetThickness(FSolverReal InThickness) { Thickness = FMath::Max(InThickness, (FSolverReal)0.);  }

protected:
	TArray<TVec4<int32>> Constraints;
	TArray<FSolverVec3> Barys;
	TArray<FSolverVec3> Normals;

private:
	const TArray<TVec3<int32>>& Elements;
	const TSet<TVec2<int32>> DisabledCollisionElements;  // TODO: Make this a bitarray
	int32 Offset;
	int32 NumParticles;
	FSolverReal Thickness;
	FSolverReal Stiffness;
};

}  // End namespace Chaos::Softs

#endif
