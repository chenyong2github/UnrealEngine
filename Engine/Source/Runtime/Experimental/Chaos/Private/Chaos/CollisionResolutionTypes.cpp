// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDCollisionConstraints.h"

namespace Chaos
{
	template class CHAOS_API TPBDCollisionConstraintHandle<float, 3>; 

	FString TCollisionContact<float, 3>::ToString() const
	{
		return FString::Printf(TEXT("Location:%s, Normal:%s, Phi:%f"), *Location.ToString(), *Normal.ToString(), Phi);
	}
};
