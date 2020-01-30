// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleRule.h"

namespace Chaos
{
template<typename T, int d>
class TPBDAnimDriveConstraint : public TParticleRule<T, d>
{
  public:
	// InSpringNeutralPositions starts at index: InParticleIndexOffset
	// InSpringStiffnessMultiplier starts at index: 0
	TPBDAnimDriveConstraint(
		const int InParticleIndexOffset
		, const TArray<TVector<T, d>>* const InSpringNeutralPositions
		, const TArray<T>* const InSpringStiffnessMultiplier
		, const T InSpringStiffness
	)
		: ParticleIndexOffset(InParticleIndexOffset)
		, SpringStiffness(InSpringStiffness)
		, SpringNeutralPositions(InSpringNeutralPositions)
		, SpringStiffnessMultiplier(InSpringStiffnessMultiplier)
	{

	}
	virtual ~TPBDAnimDriveConstraint() {}

	inline virtual void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override
	{
		int32 ConstraintCount = SpringStiffnessMultiplier->Num();
		PhysicsParallelFor(ConstraintCount, [&](int32 Index)
		{
			ApplyAnimDriveConstraint(InParticles, Dt, Index);
		});
	}

	inline void ApplyAnimDriveConstraint(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
	{
		
		if (InParticles.InvM(Index + ParticleIndexOffset) == 0.0f)
		{
			return;
		}

		const TVector<T, d> NeutralPosition = (*SpringNeutralPositions)[Index + ParticleIndexOffset];
		const TVector<T, d> ParticlePosition = InParticles.P(Index + ParticleIndexOffset);

		const T RelaxationFactor = FMath::Min(SpringStiffness * (*SpringStiffnessMultiplier)[Index], 1.0f);
		InParticles.P(Index + ParticleIndexOffset) = RelaxationFactor * NeutralPosition + (1 - RelaxationFactor) * ParticlePosition;
	}

	inline void SetSpringStiffness(T InSpringStiffness)
	{
		SpringStiffness = InSpringStiffness;
	}

private:
	const int32 ParticleIndexOffset;
	T SpringStiffness;
	const TArray<TVector<T, d>>* SpringNeutralPositions; // Size: Same as full particle array // Starts at Index: ParticleIndexOffset
	const TArray<T>* SpringStiffnessMultiplier; // Size: Number of Animation drive constraints to solve // Starts at Index 0
};
}
