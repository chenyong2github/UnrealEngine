// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidSpringConstraints;

	template<class T, int d>
	class CHAOS_API TPBDRigidSpringConstraintHandle : public TContainerConstraintHandle<TPBDRigidSpringConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDRigidSpringConstraints<T, d>>;
		using FConstraintContainer = TPBDRigidSpringConstraints<T, d>;
		using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;

		TPBDRigidSpringConstraintHandle() {}
		TPBDRigidSpringConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBDRigidSpringConstraints<T, d>>(InConstraintContainer, InConstraintIndex) {}

		const TVector<TVector<T, 3>, 2>& GetConstraintPositions() const;
		void SetConstraintPositions(const TVector<TVector<T, 3>, 2>& ConstraintPositions);
		TVector<FGeometryParticleHandle*, 2> GetConstrainedParticles() const { return ConstraintContainer->GetConstrainedParticles(ConstraintIndex); }

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};


	template<class T, int d>
	class TPBDRigidSpringConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;
		using FReal = T;
		static const int Dimensions = d;
		using FConstraintContainerHandle = TPBDRigidSpringConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDRigidSpringConstraints<FReal, Dimensions>>;
		using FConstrainedParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		TPBDRigidSpringConstraints(const T InStiffness = (T)1)
			: Stiffness(InStiffness) 
		{}

		TPBDRigidSpringConstraints(const TArray<TVector<T, 3>>& Locations0, const TArray<TVector<T, 3>>& Locations1, TArray<FConstrainedParticlePair>&& InConstraints, const T InStiffness = (T)1)
			: Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
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
					UpdateDistance(ConstraintIndex, Locations0[ConstraintIndex], Locations1[ConstraintIndex]);
				}
			}
		}

		virtual ~TPBDRigidSpringConstraints() {}

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
		FConstraintContainerHandle* AddConstraint(const FConstrainedParticlePair& InConstrainedParticles, const  TVector<TVector<T, 3>, 2>& InLocations)
		{
			Handles.Add(HandleAllocator.AllocHandle(this, Handles.Num()));
			int32 ConstraintIndex = Constraints.Add(InConstrainedParticles);
			Distances.Add({});
			SpringDistances.Add({});
			UpdateDistance(ConstraintIndex, InLocations[0], InLocations[1]);
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
			Handles.RemoveAtSwap(ConstraintIndex);

			// Update the handle for the constraint that was moved
			if (ConstraintIndex < Handles.Num())
			{
				FConstraintHandle* Handle = Handles[ConstraintIndex];
				SetConstraintIndex(Handle, ConstraintIndex);
			}
		}

		// @todo(ccaulfield): rename/remove  this
		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
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

		void Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

		bool ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
		{
			return false;
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplySingle(const T Dt, int32 ConstraintIndex) const;

		void UpdateDistance(int32 ConstraintIndex, const TVector<T, d>& Location0, const TVector<T, d>& Location1);

		TVector<T, d> GetDelta(int32 ConstraintIndex, const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2) const;

		TArray<FConstrainedParticlePair> Constraints;
		TArray<TVector<TVector<T, 3>, 2>> Distances;
		TArray<T> SpringDistances;
		T Stiffness;

		TArray<FConstraintContainerHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
