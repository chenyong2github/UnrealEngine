// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/PBDCollisionConstraints.h"

namespace Chaos
{
	typename FPBDCollisionConstraintHandle::FHandleKey FPBDCollisionConstraintHandle::GetKey()
	{
		const FCollisionConstraintBase& Contact = GetContact();
		return FHandleKey(
			FImplicitPair(Contact.Manifold.Implicit[0], Contact.Manifold.Implicit[1]),
			FGeometryPair(Contact.Particle[0], Contact.Particle[1]));
	}

	const FCollisionConstraintBase& FPBDCollisionConstraintHandle::GetContact() const
	{
		if (GetType() == FCollisionConstraintBase::FType::SinglePoint)
		{
			return ConstraintContainer->Constraints.SinglePointConstraints[ConstraintIndex];
		}
		else
		{
			check(GetType() == FCollisionConstraintBase::FType::SinglePointSwept);
			return ConstraintContainer->Constraints.SinglePointSweptConstraints[ConstraintIndex];
		}
	}

	FCollisionConstraintBase& FPBDCollisionConstraintHandle::GetContact()
	{
		if (GetType() == FCollisionConstraintBase::FType::SinglePoint)
		{
			return ConstraintContainer->Constraints.SinglePointConstraints[ConstraintIndex];
		}
		else
		{
			check(GetType() == FCollisionConstraintBase::FType::SinglePointSwept);
			return ConstraintContainer->Constraints.SinglePointSweptConstraints[ConstraintIndex];
		}
	}

	const FRigidBodyPointContactConstraint& FPBDCollisionConstraintHandle::GetPointContact() const
	{ 
		check(GetType() == FCollisionConstraintBase::FType::SinglePoint);
		return ConstraintContainer->Constraints.SinglePointConstraints[ConstraintIndex]; 
	}
	
	FRigidBodyPointContactConstraint& FPBDCollisionConstraintHandle::GetPointContact() 
	{ 
		check(GetType() == FCollisionConstraintBase::FType::SinglePoint);
		return ConstraintContainer->Constraints.SinglePointConstraints[ConstraintIndex];
	}

	const FRigidBodySweptPointContactConstraint& FPBDCollisionConstraintHandle::GetSweptPointContact() const
	{
		check(GetType() == FCollisionConstraintBase::FType::SinglePointSwept);
		return ConstraintContainer->Constraints.SinglePointSweptConstraints[ConstraintIndex];
	}
	
	FRigidBodySweptPointContactConstraint& FPBDCollisionConstraintHandle::GetSweptPointContact()
	{
		check(GetType() == FCollisionConstraintBase::FType::SinglePointSwept);
		return ConstraintContainer->Constraints.SinglePointSweptConstraints[ConstraintIndex];
	}

}