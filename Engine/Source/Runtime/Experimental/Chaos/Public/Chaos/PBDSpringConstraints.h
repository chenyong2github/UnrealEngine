// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/GraphColoring.h"

namespace Chaos
{

template<class T, int d>
class TDynamicParticles;

template<class T, int d>
class TPBDParticles;

template<class T, int d>
class TRigidParticles;

template<class T, int d>
class TPBDRigidParticles;

class CHAOS_API FPBDSpringConstraints : public TPBDSpringConstraintsBase<FReal, 3>, public FPBDConstraintContainer
{
	typedef TPBDSpringConstraintsBase<FReal, 3> Base;
	using Base::MConstraints;

  public:
	FPBDSpringConstraints(const FReal Stiffness = (FReal)1)
	    : TPBDSpringConstraintsBase<FReal, 3>(Stiffness) 
	{}

	FPBDSpringConstraints(const TDynamicParticles<FReal, 3>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const FReal Stiffness = (FReal)1)
	    : TPBDSpringConstraintsBase<FReal, 3>(InParticles, MoveTemp(Constraints), Stiffness) 
	{
		MConstraintsPerColor = FGraphColoring::ComputeGraphColoring(MConstraints, InParticles);
	}
	FPBDSpringConstraints(const TRigidParticles<FReal, 3>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const FReal Stiffness = (FReal)1)
	    : TPBDSpringConstraintsBase<FReal, 3>(InParticles, MoveTemp(Constraints), Stiffness) 
	{}

	FPBDSpringConstraints(const TDynamicParticles<FReal, 3>& InParticles, const TArray<TVector<int32, 3>>& Constraints, const FReal Stiffness = (FReal)1)
	    : TPBDSpringConstraintsBase<FReal, 3>(InParticles, Constraints, Stiffness) 
	{
		MConstraintsPerColor = FGraphColoring::ComputeGraphColoring(MConstraints, InParticles);
	}

	FPBDSpringConstraints(const TDynamicParticles<FReal, 3>& InParticles, const TArray<TVector<int32, 4>>& Constraints, const FReal Stiffness = (FReal)1)
	    : TPBDSpringConstraintsBase<FReal, 3>(InParticles, Constraints, Stiffness) 
	{
		MConstraintsPerColor = FGraphColoring::ComputeGraphColoring(MConstraints, InParticles);
	}

	virtual ~FPBDSpringConstraints() {}

	const TArray<TVector<int32, 2>>& GetConstraints() const { return MConstraints; }
	TArray<TVector<int32, 2>>& GetConstraints() { return MConstraints; }
	TArray<TVector<int32, 2>>& Constraints() { return MConstraints; }

	template<class T_PARTICLES>
	void Apply(T_PARTICLES& InParticles, const FReal Dt, const int32 InConstraintIndex) const;
	void Apply(TPBDParticles<FReal, 3>& InParticles, const FReal Dt) const;
	void Apply(TPBDRigidParticles<FReal, 3>& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices) const;

	TArray<TArray<int32>> MConstraintsPerColor;
};

}
