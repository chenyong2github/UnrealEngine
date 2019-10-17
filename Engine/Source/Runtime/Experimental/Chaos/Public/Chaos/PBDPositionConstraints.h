// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDPositionConstraints;

	template<class T, int d>
	class TPBDPositionConstraintHandle : public TContainerConstraintHandle<TPBDPositionConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDPositionConstraints<T, d>>;
		using FConstraintContainer = TPBDPositionConstraints<T, d>;

		TPBDPositionConstraintHandle() {}
		TPBDPositionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBDPositionConstraints<T, d>>(InConstraintContainer, InConstraintIndex) {}

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	template<class T, int d>
	class TPBDPositionConstraints : public TPBDConstraintContainer<T, d>
	{
	public:
		using Base = TPBDConstraintContainer<T, d>;
		using FReal = T;
		static const int Dimensions = d;
		using FConstraintHandle = TPBDPositionConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDPositionConstraints<FReal, Dimensions>>;

		TPBDPositionConstraints(const T InStiffness = (T)1)
			: Stiffness(InStiffness)
		{}

		TPBDPositionConstraints(TArray<TVector<T, d>>&& Locations, TArray<TPBDRigidParticleHandle<T,d>*>&& InConstrainedParticles, const T InStiffness = (T)1)
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

		virtual ~TPBDPositionConstraints() {}


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
		FConstraintHandle* AddConstraint(TPBDRigidParticleHandle<T, d>* Particle, const TVector<T, d>& Position)
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
			FConstraintHandle* ConstraintHandle = Handles[ConstraintIndex];
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

		// @todo(ccaulfield): remove/rename/implement
		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
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
		TVector<TGeometryParticleHandle<T, d>*, 2> GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return { ConstrainedParticles[ConstraintIndex], nullptr };
		}

		/**
		 * Get the world-space constraint positions for each body.
		 */
		const TVector<T, d>& GetConstraintPosition(int ConstraintIndex) const
		{
			return Targets[ConstraintIndex];
		}

		// @todo(ccaulfield): remove/rename
		void Replace(const int32 ConstraintIndex, const TVector<T, d>& Position)
		{
			Targets[ConstraintIndex] = Position;
		}


		//
		// Island Rule API
		//

		void UpdatePositionBasedState(const T Dt)
		{
		}

		void Apply(const T Dt, const TArray<FConstraintHandle*>& ConstraintHandles, const int32 It, const int32 NumIts) const
		{
			for (FConstraintHandle* ConstraintHandle : ConstraintHandles)
			{
				ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
			}
		}

		void ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintIndices) const
		{
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplySingle(const T Dt, int32 ConstraintIndex) const
		{
			if (TPBDRigidParticleHandle<T, d>* PBDRigid = ConstrainedParticles[ConstraintIndex])
			{
				const TVector<T, d>& P1 = PBDRigid->P();
				const TVector<T, d>& P2 = Targets[ConstraintIndex];
				TVector<T, d> Difference = P1 - P2;
				PBDRigid->P() -= Stiffness * Difference;
			}
		}

		TArray<TVector<T, d>> Targets;
		TArray<TPBDRigidParticleHandle<T,d>*> ConstrainedParticles;
		T Stiffness;

		TArray<FConstraintHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
