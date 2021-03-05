// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	class FPBDRigidDynamicSpringConstraints;

	class CHAOS_API FPBDRigidDynamicSpringConstraintHandle : public TContainerConstraintHandle<FPBDRigidDynamicSpringConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDRigidDynamicSpringConstraints>;
		using FConstraintContainer = FPBDRigidDynamicSpringConstraints;

		FPBDRigidDynamicSpringConstraintHandle() 
		{
		}

		FPBDRigidDynamicSpringConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) 
			: TContainerConstraintHandle<FPBDRigidDynamicSpringConstraints>(StaticType(),InConstraintContainer, InConstraintIndex) 
		{
		}

		static FConstraintHandle::EType StaticType() { return FConstraintHandle::EType::DynamicSpring; }
		TVec2<FGeometryParticleHandle*> GetConstrainedParticles() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	class CHAOS_API FPBDRigidDynamicSpringConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;
		using FConstrainedParticlePair = TVec2<FGeometryParticleHandle*>;
		//static const int Dimensions = 3;
		using FConstraintContainerHandle = FPBDRigidDynamicSpringConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDRigidDynamicSpringConstraints>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		FPBDRigidDynamicSpringConstraints(const FReal InStiffness = (FReal)1.)
			: CreationThreshold(1), MaxSprings(1), Stiffness(InStiffness) 
		{}

		FPBDRigidDynamicSpringConstraints(TArray<FConstrainedParticlePair>&& InConstraints, const FReal InCreationThreshold = (FReal)1., const int32 InMaxSprings = 1, const FReal InStiffness = (FReal)1.)
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

		virtual ~FPBDRigidDynamicSpringConstraints() {}

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
		FConstraintContainerHandle* AddConstraint(const FConstrainedParticlePair& InConstrainedParticles)
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
			FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintIndex];
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

		/**
		 * Disabled the specified constraint.
		 */
		void DisableConstraints(const TSet<FGeometryParticleHandle*>& RemovedParticles)
		{
			// @todo(chaos)
		}


		/**
		 * Set the distance threshold below which springs get created between particles.
		 */
		void SetCreationThreshold(const FReal InCreationThreshold)
		{
			CreationThreshold = InCreationThreshold;
		}

		/**
		 * Set the maximum number of springs
		 */
		void SetMaxSprings(const FReal InMaxSprings)
		{
			MaxSprings = InMaxSprings;
		}


		//
		// Constraint API
		//
		FHandles& GetConstraintHandles()
		{
			return Handles;
		}
		const FHandles& GetConstConstraintHandles() const
		{
			return Handles;
		}


		const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const
		{
			return Handles[ConstraintIndex];
		}

		FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex)
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

		void PrepareTick() {}

		void UnprepareTick() {}

		void PrepareIteration(FReal Dt) {}

		void UnprepareIteration(FReal Dt) {}

		void UpdatePositionBasedState(const FReal Dt);

		bool Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
		{
			for (FConstraintContainerHandle* ConstraintHandle : InConstraintHandles)
			{
				ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
			}

			// TODO: Return true only if more iteration are needed
			return true;
		}

		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
		{
			return false;
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplySingle(const FReal Dt, int32 ConstraintIndex) const;

		FVec3 GetDelta(const FVec3& WorldSpaceX1, const FVec3& WorldSpaceX2, const int32 ConstraintIndex, const int32 SpringIndex) const;

		TArray<FConstrainedParticlePair> Constraints;
		TArray<TArray<TVec2<FVec3>>> Distances;
		TArray<TArray<FReal>> SpringDistances;
		FReal CreationThreshold;
		int32 MaxSprings;
		FReal Stiffness;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
	};

	template<class T, int d>
	using TPBDRigidDynamicSpringConstraintHandle UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDRigidDynamicSpringConstraintHandle instead") = FPBDRigidDynamicSpringConstraintHandle;

	template<class T, int d>
	using TPBDRigidDynamicSpringConstraints UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDRigidDynamicSpringConstraints instead") = FPBDRigidDynamicSpringConstraints;
}
