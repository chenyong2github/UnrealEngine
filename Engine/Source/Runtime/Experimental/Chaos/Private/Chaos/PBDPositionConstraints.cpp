// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDPositionConstraints.h"


namespace Chaos
{

	template<class T, int d>
	void TPBDPositionConstraints<T, d>::Apply(const T Dt, const TArray<FConstraintContainerHandle*>& ConstraintHandles, const int32 It, const int32 NumIts) const
	{
		for (FConstraintContainerHandle* ConstraintHandle : ConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
		}
	}



	template class TPBDPositionConstraintHandle<float, 3>;
	template class TPBDPositionConstraints<float, 3>;
}
