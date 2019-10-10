// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
	template<class T, int d>
	class CHAOS_API TPBDConstraintContainer
	{
	public:
		TPBDConstraintContainer();

		virtual ~TPBDConstraintContainer();

	protected:
		// friend access to the Constraint Handle's container API
		int32 GetConstraintIndex(const TConstraintHandle<T, d>* ConstraintHandle) const;
		void SetConstraintIndex(TConstraintHandle<T, d>* ConstraintHandle, int32 ConstraintIndex) const;
	};
}