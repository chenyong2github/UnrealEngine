// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	class FPBDCollisionConstraints;

	class CHAOS_API FPBDCollisionConstraintHandle : public TContainerConstraintHandle<FPBDCollisionConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDCollisionConstraints>;
		using FImplicitPair = TPair<const FImplicitObject*, const FImplicitObject*>;
		using FGeometryPair = TPair<const TGeometryParticleHandle<FReal, 3>*, const TGeometryParticleHandle<FReal, 3>*>;
		using FHandleKey = TPair<FImplicitPair, FGeometryPair>;


		FPBDCollisionConstraintHandle()
			: ConstraintType(FCollisionConstraintBase::FType::None)
		{}

		FPBDCollisionConstraintHandle(FPBDCollisionConstraints* InConstraintContainer, int32 InConstraintIndex, typename FCollisionConstraintBase::FType InType)
			: TContainerConstraintHandle<FPBDCollisionConstraints>(StaticType(), InConstraintContainer, InConstraintIndex)
			, ConstraintType(InType)
		{
		}
		static FConstraintHandle::EType StaticType() { return FConstraintHandle::EType::Collision; }


		FHandleKey GetKey();

		static FHandleKey MakeKey(const TGeometryParticleHandle<FReal, 3>* Particle0, const TGeometryParticleHandle<FReal, 3>* Particle1,
			const FImplicitObject* Implicit0, const FImplicitObject* Implicit1)
		{
			return FHandleKey(FImplicitPair(Implicit0, Implicit1), FGeometryPair(Particle0, Particle1));
		}

		static FHandleKey MakeKey(const FCollisionConstraintBase* Base)
		{
			return FHandleKey(FImplicitPair(Base->Manifold.Implicit[0], Base->Manifold.Implicit[1]), FGeometryPair(Base->Particle[0], Base->Particle[1]));
		}


		const FCollisionConstraintBase& GetContact() const;
		FCollisionConstraintBase& GetContact();

		const FRigidBodyPointContactConstraint& GetPointContact() const;
		FRigidBodyPointContactConstraint& GetPointContact();

		const FRigidBodySweptPointContactConstraint& GetSweptPointContact() const;
		FRigidBodySweptPointContactConstraint& GetSweptPointContact();

		typename FCollisionConstraintBase::FType GetType() const { return ConstraintType; }

		void SetConstraintIndex(int32 IndexIn, typename FCollisionConstraintBase::FType InType)
		{
			ConstraintIndex = IndexIn;
			ConstraintType = InType;
		}

		FVec3 GetContactLocation() const
		{
			return GetContact().GetLocation();
		}

		FVec3 GetAccumulatedImpulse() const
		{
			return GetContact().AccumulatedImpulse;
		}

		TVector<const TGeometryParticleHandle<FReal, 3>*, 2> GetConstrainedParticles() const
		{
			return { GetContact().Particle[0], GetContact().Particle[1] };
		}

		TVector<TGeometryParticleHandle<FReal, 3>*, 2> GetConstrainedParticles()
		{
			return { GetContact().Particle[0], GetContact().Particle[1] };
		}


	protected:
		typename FCollisionConstraintBase::FType ConstraintType;
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;


	};
}