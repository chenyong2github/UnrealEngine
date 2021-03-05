// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDPositionConstraints.h"

namespace Chaos
{
	TVector<FGeometryParticleHandle*, 2> FPBDPositionConstraintHandle::GetConstrainedParticles() const 
	{ 
		return ConstraintContainer->GetConstrainedParticles(ConstraintIndex); 
	}

	bool FPBDPositionConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& ConstraintHandles, const int32 It, const int32 NumIts) const
	{
		for (FConstraintContainerHandle* ConstraintHandle : ConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
		}

		// TODO: Return true only if more iteration are needed
		return true;
	}
}
