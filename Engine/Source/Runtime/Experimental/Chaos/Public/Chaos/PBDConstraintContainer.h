// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Transform.h"

namespace Chaos
{
	/**
	 * Base class for containers of constraints.
	 * A Constraint Container holds an array of constraints and provides methods to allocate and deallocate constraints
	 *as well as the API required to plug into Constraint Rules.
	 */
	class CHAOS_API FPBDConstraintContainer
	{
	public:
		FPBDConstraintContainer();

		virtual ~FPBDConstraintContainer();

	protected:
		// friend access to the Constraint Handle's container API
		int32 GetConstraintIndex(const FConstraintHandle* ConstraintHandle) const;
		void SetConstraintIndex(FConstraintHandle* ConstraintHandle, int32 ConstraintIndex) const;
	};
}