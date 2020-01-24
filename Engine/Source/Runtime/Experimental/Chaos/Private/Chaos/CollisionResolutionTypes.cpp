// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDCollisionConstraints.h"

namespace Chaos
{
	template<typename T, int d>
	FString TCollisionContact<T,d>::ToString() const
	{
		return FString::Printf(TEXT("Location:%s, Normal:%s, Phi:%f"), *Location.ToString(), *Normal.ToString(), Phi);
	}

	template class TPBDCollisionConstraintHandle<float, 3>; 
	template class TCollisionConstraintBase<float, 3>;
};
