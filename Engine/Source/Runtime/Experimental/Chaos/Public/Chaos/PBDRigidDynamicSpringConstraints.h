// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidDynamicSpringConstraints;

	template<class T, int d>
	class CHAOS_API TPBDRigidDynamicSpringConstraintHandle : public TContainerConstraintHandle<TPBDRigidDynamicSpringConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDRigidDynamicSpringConstraints<T, d>>;
		using FConstraintContainer = TPBDRigidDynamicSpringConstraints<T, d>;

		TPBDRigidDynamicSpringConstraintHandle() {}
		TPBDRigidDynamicSpringConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBDRigidDynamicSpringConstraints<T, d>>(InConstraintContainer, InConstraintIndex) {}

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	template<class T, int d>
	class CHAOS_API TPBDRigidDynamicSpringConstraints : public TPBDConstraintContainer<T, d>
	{
	public:
		using Base = TPBDConstraintContainer<T, d>;
		using FConstrainedParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;
		using FReal = T;
		static const int Dimensions = d;
		using FConstraintHandle = TPBDRigidDynamicSpringConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDRigidDynamicSpringConstraints<FReal, Dimensions>>;

		TPBDRigidDynamicSpringConstraints(const T InStiffness = (T)1)
			: CreationThreshold(1), MaxSprings(1), Stiffness(InStiffness) 
		{}

		TPBDRigidDynamicSpringConstraints(TArray<FConstrainedParticlePair>&& InConstraints, const T InCreationThreshold = (T)1, const int32 InMaxSprings = 1, const T InStiffness = (T)1)
			: Constraints(MoveTemp(InConstraints)), CreationThreshold(InCreationThreshold), MaxSprings(InMaxSprings), Stiffness(InStiffness)
		{
			if (Constraints.Num() > 0)
			{
				Handles.Reserve(Constraints.Num());
				Distances.Reserve(Constraints.Num());
				SpringDistances.Reserve(Constraints.Num());
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
				{
					Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
					Distances.Add({});
					SpringDistances.Add({});
				}
			}
		}

		virtual ~TPBDRigidDynamicSpringConstraints() {}

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		/**
		 * Add a constraint initialized from current world-space particle positions.
		 * You would use this method when your objects are already positioned in the world.
		 */
		FConstraintHandle* AddConstraint(const FConstrainedParticlePair& InConstrainedParticles)
		{
			Handles.Add(HandleAllocator.AllocHandle(this, Handles.Num()));
			Constraints.Add(InConstrainedParticles);
			Distances.Add({});
			SpringDistances.Add({});
			return Handles.Last();
		}

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex)
		{
			FConstraintHandle* ConstraintHandle = Handles[ConstraintIndex];
			if (ConstraintHandle != nullptr)
			{
				// Release the handle for the freed constraint
				HandleAllocator.FreeHandle(ConstraintHandle);
				Handles[ConstraintIndex] = nullptr;
			}

			// Swap the last constraint into the gap to keep the array packed
			Constraints.RemoveAtSwap(ConstraintIndex);
			Distances.RemoveAtSwap(ConstraintIndex);
			SpringDistances.RemoveAtSwap(ConstraintIndex);
			Handles.RemoveAtSwap(ConstraintIndex);

			// Update the handle for the constraint that was moved
			if (ConstraintIndex < Handles.Num())
			{
				SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
			}
		}

		// @todo(ccaulfield): rename/remove  this
		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
		}

		/**
		 * Set the distance threshold below which springs get created between particles.
		 */
		void SetCreationThreshold(const T InCreationThreshold)
		{
			CreationThreshold = InCreationThreshold;
		}

		/**
		 * Set the maximum number of springs
		 */
		void SetMaxSprings(const T InMaxSprings)
		{
			MaxSprings = InMaxSprings;
		}


		//
		// Constraint API
		//

		const FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex) const
		{
			return Handles[ConstraintIndex];
		}

		FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex)
		{
			return Handles[ConstraintIndex];
		}

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		const FConstrainedParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}


		//
		// Island Rule API
		//

		void UpdatePositionBasedState(const T Dt);

		void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
		{
			for (FConstraintHandle* ConstraintHandle : InConstraintHandles)
			{
				ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
			}
		}

		// @todo(ccaulfield): remove  this
		void ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles)
		{
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplySingle(const T Dt, int32 ConstraintIndex) const;

		TVector<T, d> GetDelta(const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const int32 ConstraintIndex, const int32 SpringIndex) const;

		TArray<FConstrainedParticlePair> Constraints;
		TArray<TArray<TVector<TVector<T, 3>, 2>>> Distances;
		TArray<TArray<T>> SpringDistances;
		T CreationThreshold;
		int32 MaxSprings;
		T Stiffness;

		TArray<FConstraintHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
