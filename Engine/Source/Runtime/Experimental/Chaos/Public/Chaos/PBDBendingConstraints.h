// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDBendingConstraintsBase.h"

namespace Chaos::Softs
{

class FPBDBendingConstraints : public FPBDBendingConstraintsBase
{
	typedef FPBDBendingConstraintsBase Base;
	using Base::Constraints;

public:
	FPBDBendingConstraints(const FSolverParticles& InParticles,
		int32 ParticleOffset,
		int32 ParticleCount,
		TArray<TVec4<int32>>&& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers,
		const FSolverVec2& InStiffness,
		const FSolverReal InBucklingRatio,
		const FSolverVec2& InBucklingStiffness,
		bool bTrimKinematicConstraints = false)
		:Base(InParticles, ParticleOffset, ParticleCount, MoveTemp(InConstraints), StiffnessMultipliers, BucklingStiffnessMultipliers, InStiffness, InBucklingRatio, InBucklingStiffness, bTrimKinematicConstraints) {}

	FPBDBendingConstraints(const FSolverParticles& InParticles, TArray<TVec4<int32>>&& InConstraints, const FSolverReal InStiffness = (FSolverReal)1.)
	    : Base(InParticles, MoveTemp(InConstraints), InStiffness) {}


	virtual ~FPBDBendingConstraints() override {}

	void Apply(FSolverParticles& InParticles, const FSolverReal Dt) const
	{
		const bool StiffnessHasWeightMap = Stiffness.HasWeightMap();
		const bool BucklingStiffnessHasWeightMap = BucklingStiffness.HasWeightMap();
		if (!StiffnessHasWeightMap && !BucklingStiffnessHasWeightMap)
		{
			const FSolverReal ExpStiffnessValue = (FSolverReal)Stiffness;
			const FSolverReal ExpBucklingValue = (FSolverReal)BucklingStiffness;

			for (int i = 0; i < Constraints.Num(); ++i)
			{
				const TVec4<int32>& Constraint = Constraints[i];
				const int32 i1 = Constraint[0];
				const int32 i2 = Constraint[1];
				const int32 i3 = Constraint[2];
				const int32 i4 = Constraint[3];
				const TStaticArray<FSolverVec3, 4> Grads = Base::GetGradients(InParticles, i);
				const FSolverReal S = Base::GetScalingFactor(InParticles, i, Grads, ExpStiffnessValue, ExpBucklingValue);
				InParticles.P(i1) -= S * InParticles.InvM(i1) * Grads[0];
				InParticles.P(i2) -= S * InParticles.InvM(i2) * Grads[1];
				InParticles.P(i3) -= S * InParticles.InvM(i3) * Grads[2];
				InParticles.P(i4) -= S * InParticles.InvM(i4) * Grads[3];
			}
		}
		else
		{
			const FSolverReal StiffnessNoMap = (FSolverReal)Stiffness;
			const FSolverReal BucklingStiffnessNoMap = (FSolverReal)BucklingStiffness;
			for (int i = 0; i < Constraints.Num(); ++i)
			{
				const FSolverReal ExpStiffnessValue = StiffnessHasWeightMap ? Stiffness[i] : StiffnessNoMap;
				const FSolverReal ExpBucklingValue = BucklingStiffnessHasWeightMap ? BucklingStiffness[i] : BucklingStiffnessNoMap;
				const TVec4<int32>& Constraint = Constraints[i];
				const int32 i1 = Constraint[0];
				const int32 i2 = Constraint[1];
				const int32 i3 = Constraint[2];
				const int32 i4 = Constraint[3];
				const TStaticArray<FSolverVec3, 4> Grads = Base::GetGradients(InParticles, i);
				const FSolverReal S = Base::GetScalingFactor(InParticles, i, Grads, ExpStiffnessValue, ExpBucklingValue);
				InParticles.P(i1) -= S * InParticles.InvM(i1) * Grads[0];
				InParticles.P(i2) -= S * InParticles.InvM(i2) * Grads[1];
				InParticles.P(i3) -= S * InParticles.InvM(i3) * Grads[2];
				InParticles.P(i4) -= S * InParticles.InvM(i4) * Grads[3];
			}
		}
	}
};

}  // End namespace Chaos::Softs
