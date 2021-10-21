// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	FPBDConstraintContainer::FPBDConstraintContainer(FConstraintHandleTypeID InConstraintHandleType)
		: ConstraintHandleType(InConstraintHandleType)
		, ContainerId(INDEX_NONE)
	{
	}

	FPBDConstraintContainer::~FPBDConstraintContainer()
	{
	}

	int32 FPBDIndexedConstraintContainer::GetConstraintIndex(const FIndexedConstraintHandle* ConstraintHandle) const
	{
		return ConstraintHandle->GetConstraintIndex();
	}

	void FPBDIndexedConstraintContainer::SetConstraintIndex(FIndexedConstraintHandle* ConstraintHandle, int32 ConstraintIndex) const
	{
		ConstraintHandle->ConstraintIndex = ConstraintIndex;
	}
}
