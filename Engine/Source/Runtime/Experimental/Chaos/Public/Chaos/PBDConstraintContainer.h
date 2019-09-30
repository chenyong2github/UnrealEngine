// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Defines.h"
#include "Chaos/ConstraintHandle.h"

namespace Chaos
{
	/**
	 * Base class for containers of constraints.
	 * A Constraint Container holds an array of constraints and provides methods to allocate and deallocate constraints
	 *as well as the API required to plug into Constraint Rules.
	 */
	template<class T, int d>
	class CHAOS_API TPBDConstraintContainer
	{
	public:
		using FConstraintHandle = TConstraintHandle<T, d>;

	protected:
		// friend access to the Constraint Handle's container API
		int32 GetConstraintIndex(const FConstraintHandle* ConstraintHandle) const { return ConstraintHandle->ConstraintIndex; }
		void SetConstraintIndex(FConstraintHandle* ConstraintHandle, int32 ConstraintIndex) const { ConstraintHandle->ConstraintIndex = ConstraintIndex; }
	};
}