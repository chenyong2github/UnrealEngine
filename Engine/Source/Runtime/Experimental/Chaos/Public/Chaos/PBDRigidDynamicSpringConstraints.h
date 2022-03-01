// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/Evolution/SolverConstraintContainer.h"

namespace Chaos
{
	class FPBDRigidDynamicSpringConstraints;
	class FPBDIslandSolverData;

	class CHAOS_API FPBDRigidDynamicSpringConstraintHandle : public TIndexedContainerConstraintHandle<FPBDRigidDynamicSpringConstraints>
	{
	public:
		using Base = TIndexedContainerConstraintHandle<FPBDRigidDynamicSpringConstraints>;
		using FConstraintContainer = FPBDRigidDynamicSpringConstraints;

		FPBDRigidDynamicSpringConstraintHandle() 
		{
		}

		FPBDRigidDynamicSpringConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) 
			: TIndexedContainerConstraintHandle<FPBDRigidDynamicSpringConstraints>(InConstraintContainer, InConstraintIndex)
		{
		}

		TVec2<FGeometryParticleHandle*> GetConstrainedParticles() const;

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FRigidDynamicSpringConstraintHandle"), &FIndexedConstraintHandle::StaticType());
			return STypeID;
		}

	protected:
		using Base::ConstraintIndex;
		using Base::ConcreteContainer;
	};

	class CHAOS_API FPBDRigidDynamicSpringConstraints : public FPBDIndexedConstraintContainer
	{
	public:
		using Base = FPBDIndexedConstraintContainer;
		using FConstrainedParticlePair = TVec2<FGeometryParticleHandle*>;
		//static const int Dimensions = 3;
		using FConstraintContainerHandle = FPBDRigidDynamicSpringConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDRigidDynamicSpringConstraints>;
		using FHandles = TArray<FConstraintContainerHandle*>;
		using FConstraintSolverContainerType = FConstraintSolverContainer;	// @todo(chaos): Add island solver for this constraint type

		FPBDRigidDynamicSpringConstraints(const FReal InStiffness = (FReal)1.)
			: FPBDIndexedConstraintContainer(FConstraintContainerHandle::StaticType())
			, CreationThreshold(1)
			, MaxSprings(1)
			, Stiffness(InStiffness) 
		{}

		FPBDRigidDynamicSpringConstraints(TArray<FConstrainedParticlePair>&& InConstraints, const FReal InCreationThreshold = (FReal)1., const int32 InMaxSprings = 1, const FReal InStiffness = (FReal)1.)
			: FPBDIndexedConstraintContainer(FConstraintContainerHandle::StaticType())
			, Constraints(MoveTemp(InConstraints))
			, CreationThreshold(InCreationThreshold)
			, MaxSprings(InMaxSprings)
			, Stiffness(InStiffness)
		{
			if (Constraints.Num() > 0)
			{
				Handles.Reserve(Constraints.Num());
				Distances.Reserve(Constraints.Num());
				SpringDistances.Reserve(Constraints.Num());
				ConstraintSolverBodies.Reserve(Constraints.Num());
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
				{
					Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
					Distances.Add({});
					SpringDistances.Add({});
					ConstraintSolverBodies.Add({ nullptr, nullptr });
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
			ConstraintSolverBodies.Add({ nullptr, nullptr });
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
			ConstraintSolverBodies.RemoveAtSwap(ConstraintIndex);
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
		void SetMaxSprings(const int32 InMaxSprings)
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
		void UpdatePositionBasedState(const FReal Dt);

		void SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData);
		void PreGatherInput(const FReal Dt, const int32 ConstraintIndex, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);
		void ScatterOutput(FReal Dt, FPBDIslandSolverData& SolverData);

		bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }

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

		TArray<FSolverBodyPtrPair> ConstraintSolverBodies;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
	};

	template<class T, int d>
	using TPBDRigidDynamicSpringConstraintHandle UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDRigidDynamicSpringConstraintHandle instead") = FPBDRigidDynamicSpringConstraintHandle;

	template<class T, int d>
	using TPBDRigidDynamicSpringConstraints UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDRigidDynamicSpringConstraints instead") = FPBDRigidDynamicSpringConstraints;
}
