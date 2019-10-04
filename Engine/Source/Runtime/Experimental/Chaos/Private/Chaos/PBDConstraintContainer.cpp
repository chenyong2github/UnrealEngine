// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<typename T, int d>
	TPBDConstraintContainer<T, d>::TPBDConstraintContainer()
	{
	}

	template<typename T, int d>
	TPBDConstraintContainer<T, d>::~TPBDConstraintContainer()
	{
	}

	template<typename T, int d>
	int32 TPBDConstraintContainer<T, d>::GetConstraintIndex(const TConstraintHandle<T, d>* ConstraintHandle) const
	{
		return ConstraintHandle->ConstraintIndex;
	}

	template<typename T, int d>
	void TPBDConstraintContainer<T, d>::SetConstraintIndex(TConstraintHandle<T, d>* ConstraintHandle, int32 ConstraintIndex) const
	{
		ConstraintHandle->ConstraintIndex = ConstraintIndex;
	}
}

namespace Chaos
{
	template class TPBDConstraintContainer<float, 3>;
}