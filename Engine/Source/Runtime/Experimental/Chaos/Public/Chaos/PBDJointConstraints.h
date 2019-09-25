// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/Matrix.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDJointConstraints;

	template<class T, int d>
	class CHAOS_API TPBDJointConstraintHandle : public TContainerConstraintHandle<TPBDJointConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDJointConstraints<T, d>>;
		using FConstraintContainer = TPBDJointConstraints<T, d>;

		TPBDJointConstraintHandle() {}
		TPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBDJointConstraints<T, d>>(InConstraintContainer, InConstraintIndex) {}

		const TVector<TVector<T, 3>, 2>& GetConstraintPositions() const;
		void SetConstraintPositions(const TVector<TVector<T, 3>, 2>& ConstraintPositions);

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};


	template<class T, int d>
	class CHAOS_API TPBDJointConstraints : public TPBDConstraintContainer<T, d>
	{
	public:
		using Base = TPBDConstraintContainer<T, d>;
		using FReal = T;
		static const int Dimensions = d;
		using FConstraintHandle = TPBDJointConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDJointConstraints<FReal, Dimensions>>;
		using FConstrainedParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;


		TPBDJointConstraints(const T InStiffness = (T)1)
			: Stiffness(InStiffness)
		{
		}

		TPBDJointConstraints(const TArray<TVector<T, d>>& Locations, TArray<FConstrainedParticlePair>&& InConstraints, const T InStiffness = (T)1)
			: Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
		{
			if (Constraints.Num() > 0)
			{
				Handles.Reserve(Constraints.Num());
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
				{
					Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
				}
			}
			UpdateDistances(Locations);
		}

		virtual ~TPBDJointConstraints() {}

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
		FConstraintHandle* AddConstraint(const FConstrainedParticlePair& InConstrainedParticles, const TVector<T, d>& InLocation)
		{
			Handles.Add(HandleAllocator.AllocHandle(this, Handles.Num()));
			int32 ConstraintIndex = Constraints.Add(InConstrainedParticles);
			UpdateDistance(InLocation, ConstraintIndex);
			return Handles.Last();
		}

		/** 
		 * Add a constraint and explicitly provide constrained particle offsets. 
		 * You would use this method to initialize a constraint when the particles are not already in the correct locations. This might be because the
		 * particles have not been positioned yet, or if the particles are not positioned correctly (it will be the constraint's job to correct the positions).
		 */
		FConstraintHandle* AddConstraintLocal(const FConstrainedParticlePair& InConstrainedParticles, const TVector<TVector<T, 3>, 2>& InLocations)
		{
			Handles.Add(HandleAllocator.AllocHandle(this, Handles.Num()));
			Constraints.Add(InConstrainedParticles);
			Distances.Add(InLocations);
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

		/**
		 * Get the local-space constraint positions for each body.
		 */
		const TVector<TVector<T, 3>, 2>& GetConstraintPositions(int ConstraintIndex) const
		{
			return Distances[ConstraintIndex];
		}

		/**
		 * Set the local-space constraint positions for each body.
		 */
		void SetConstraintPositions(int ConstraintIndex, const TVector<TVector<T, 3>, 2>& ConstraintPositions)
		{
			Distances[ConstraintIndex] = ConstraintPositions;
		}


		//
		// Island Rule API
		//

		void UpdatePositionBasedState(const T Dt)
		{
		}

		void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles)
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

		void ApplySingle(const T Dt, const int32 ConstraintIndex);

	private:
		void UpdateDistanceInternal(const TVector<T, d>& InLocation, const int32 InConstraintIndex);
		void UpdateDistance(const TVector<T, d>& InLocation, const int32 InConstraintIndex);
		void UpdateDistances(const TArray<TVector<T, d>>& InLocations);

		// Double dynamic body solve
		void ApplyDynamicDynamic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const bool bApplyProjection);
		TVector<T, d> GetDeltaDynamicDynamic(const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& WorldSpaceX0, const TVector<T, d>& WorldSpaceX1, const PMatrix<T, d, d>& WorldSpaceInvI0, const PMatrix<T, d, d>& WorldSpaceInvI1, const T InvM0, const T InvM1);

		// Single dynamic body solve
		void ApplyDynamicStatic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index, const bool bApplyProjection);
		TVector<T, d> GetDeltaDynamicKinematic(const TVector<T, d>& P0, const TVector<T, d>& WorldSpaceX0, const TVector<T, d>& WorldSpaceX1, const PMatrix<T, d, d>& WorldSpaceInvI0, const T InvM0);

		// @todo(ccaulfield): we never iterate over these separately. Do we still want SoA?
		TArray<FConstrainedParticlePair> Constraints;
		TArray<TVector<TVector<T, 3>, 2>> Distances;
		T Stiffness;

		TArray<FConstraintHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
