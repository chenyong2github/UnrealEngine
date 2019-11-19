// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDCollisionConstraint.h"

namespace Chaos
{

	template<class T, int d> template<class TYPE>
	TYPE& TPBDCollisionConstraintHandle<T, d>::GetContact()
	{
		return ConstraintContainer->Constraints[ConstraintIndex];
	}

	template<class T, int d> template<class TYPE>
	const TYPE& TPBDCollisionConstraintHandle<T, d>::GetContact() const
	{
		return ConstraintContainer->Constraints[ConstraintIndex];
	}

	template class CHAOS_API TPBDCollisionConstraintHandle<float, 3>;
	template CHAOS_API TRigidBodySingleContactConstraint<float, 3>& TPBDCollisionConstraintHandle<float, 3>::GetContact<TRigidBodySingleContactConstraint<float, 3>>();
	template CHAOS_API const TRigidBodySingleContactConstraint<float, 3>& TPBDCollisionConstraintHandle<float, 3>::GetContact<TRigidBodySingleContactConstraint<float, 3>>() const;
};
