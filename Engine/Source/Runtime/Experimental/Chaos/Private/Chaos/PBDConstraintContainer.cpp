// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	FPBDConstraintContainer::FPBDConstraintContainer()
	{
	}

	FPBDConstraintContainer::~FPBDConstraintContainer()
	{
	}

	int32 FPBDConstraintContainer::GetConstraintIndex(const FConstraintHandle* ConstraintHandle) const
	{
		return ConstraintHandle->GetConstraintIndex();
	}

	void FPBDConstraintContainer::SetConstraintIndex(FConstraintHandle* ConstraintHandle, int32 ConstraintIndex) const
	{
		ConstraintHandle->ConstraintIndex = ConstraintIndex;
	}
}
