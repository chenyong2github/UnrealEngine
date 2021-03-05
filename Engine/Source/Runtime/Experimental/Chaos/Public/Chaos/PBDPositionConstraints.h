// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	class FPBDPositionConstraints;

	class FPBDPositionConstraintHandle : public TContainerConstraintHandle<FPBDPositionConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDPositionConstraints>;
		using FConstraintContainer = FPBDPositionConstraints;
		using FGeometryParticleHandle = FGeometryParticleHandle;

		FPBDPositionConstraintHandle() {}
		FPBDPositionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) 
			: TContainerConstraintHandle<FPBDPositionConstraints>(StaticType(), InConstraintContainer, InConstraintIndex) {}
		static FConstraintHandle::EType StaticType() { return FConstraintHandle::EType::Position; }
		TVector<FGeometryParticleHandle*, 2> GetConstrainedParticles() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	class FPBDPositionConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;
		//using FReal = T;
		//static const int Dimensions = 3;
		using FConstraintContainerHandle = FPBDPositionConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDPositionConstraints>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		FPBDPositionConstraints(const FReal InStiffness = (FReal)1.)
			: Stiffness(InStiffness)
		{}

		FPBDPositionConstraints(TArray<FVec3>&& Locations, TArray<FPBDRigidParticleHandle*>&& InConstrainedParticles, const FReal InStiffness = (FReal)1.)
			: Targets(MoveTemp(Locations)), ConstrainedParticles(MoveTemp(InConstrainedParticles)), Stiffness(InStiffness)
		{
			if (ConstrainedParticles.Num() > 0)
			{
				Handles.Reserve(ConstrainedParticles.Num());
				for (int32 ConstraintIndex = 0; ConstraintIndex < ConstrainedParticles.Num(); ++ConstraintIndex)
				{
					Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
				}
			}
		}

		virtual ~FPBDPositionConstraints() {}


		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const
		{
			return ConstrainedParticles.Num();
		}

		/**
		 * Add a constraint.
		 */
		FConstraintContainerHandle* AddConstraint(FPBDRigidParticleHandle* Particle, const FVec3& Position)
		{
			int32 NewIndex = Targets.Num();
			Targets.Add(Position);
			ConstrainedParticles.Add(Particle);
			Handles.Add(HandleAllocator.AllocHandle(this, NewIndex));
			return Handles[NewIndex];
		}

		/**
		 * Remove a constraint.
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
			Targets.RemoveAtSwap(ConstraintIndex);
			ConstrainedParticles.RemoveAtSwap(ConstraintIndex);
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
		TVec2<FGeometryParticleHandle*> GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return { ConstrainedParticles[ConstraintIndex], nullptr };
		}

		/**
		 * Get the world-space constraint positions for each body.
		 */
		const FVec3& GetConstraintPosition(int ConstraintIndex) const
		{
			return Targets[ConstraintIndex];
		}

		// @todo(ccaulfield): remove/rename
		void Replace(const int32 ConstraintIndex, const FVec3& Position)
		{
			Targets[ConstraintIndex] = Position;
		}


		//
		// Island Rule API
		//

		void PrepareTick() {}

		void UnprepareTick() {}

		void PrepareIteration(FReal Dt) {}

		void UnprepareIteration(FReal Dt) {}

		void UpdatePositionBasedState(const FReal Dt) {}

		bool Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& ConstraintHandles, const int32 It, const int32 NumIts) const;

		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintIndices, const int32 It, const int32 NumIts) const
		{
			return false;
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplySingle(const FReal Dt, int32 ConstraintIndex) const
		{
			if (FPBDRigidParticleHandle* PBDRigid = ConstrainedParticles[ConstraintIndex])
			{
				const FVec3& P1 = PBDRigid->P();
				const FVec3& P2 = Targets[ConstraintIndex];
				FVec3 Difference = P1 - P2;
				PBDRigid->P() -= Stiffness * Difference;
			}
		}

		TArray<FVec3> Targets;
		TArray<FPBDRigidParticleHandle*> ConstrainedParticles;
		FReal Stiffness;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
	};

	template <typename T, int d>
	using TPBDPositionConstraintHandle UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDPositionConstraintHandle instead") = FPBDPositionConstraintHandle;

	template <typename T, int d>
	using TPBDPositionConstraints UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDPositionConstraints instead") = FPBDPositionConstraints;

}
