// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Evolution/SolverBody.h"
#include "Chaos/Framework/ContainerItemHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"

#include "Containers/Array.h"
#include "Containers/Map.h"

namespace Chaos
{
	class FSolverBody;
	class FSolverBodyContainer;

	/**
	 * @brief An FSolverBody wrapper that binds the solver to a particle and provides gather/scatter methods from/to the particle
	 * Solver Bodies are used by the core constraint solver
	 * @see FSolverBody
	*/
	class FSolverBodyAdapter
	{
	public:
		FSolverBodyAdapter(const FGenericParticleHandle& InParticle)
			: Particle(InParticle)
		{
			GatherInput();
		}
		
		FSolverBody& GetSolverBody() { return SolverBody; }
		const FGenericParticleHandle& GetParticle() const { return Particle; }

		FORCEINLINE void GatherInput()
		{
			if (Particle.IsValid())
			{
				FRigidTransform3 CoMTransform = FParticleUtilitiesPQ::GetCoMWorldTransform(Particle);
				SolverBody.SetP(CoMTransform.GetLocation());
				SolverBody.SetQ(CoMTransform.GetRotation());
				SolverBody.SetV(Particle->V());
				SolverBody.SetW(Particle->W());
				SolverBody.SetCoM(Particle->CenterOfMass());
				SolverBody.SetRoM(Particle->RotationOfMass());

				if (Particle->IsDynamic())
				{
					FRigidTransform3 PrevCoMTransform = FParticleUtilitiesXR::GetCoMWorldTransform(Particle);
					SolverBody.SetX(PrevCoMTransform.GetLocation());
					SolverBody.SetR(PrevCoMTransform.GetRotation());

					SolverBody.SetInvM(Particle->InvM());
					SolverBody.SetInvILocal(Particle->InvI());
				}
				else
				{
					SolverBody.SetX(SolverBody.P());
					SolverBody.SetR(SolverBody.Q());
				}
				// No need to update the UpdateRotationDependentState since this function is only
				// valid for dynamic particle for which the SetInvILocal is already doing the job
			}
		}
		void ScatterOutput();

	private:
		FSolverBody SolverBody;
		FGenericParticleHandle Particle;
	};

	/**
	 * The SolverBodies for a set of particles.
	 * 
	 * Each Island owns a SolverBodyContainer containing the data required for solving the constraints in the island.
	 * Constraints in the island will hold pointers to SolverBodies in the Island's SolverBodyContainer.
	 * ConstraintSolvers read and write to their SolverBodies and do not access particles directly.
	 * 
	 * SolverBodies are created at the start of the constraint solving phase, and destroyed at the end. They are
	 * stored in a contiguous array in the order that they are accessed, for cache efficiency. The array
	 * is guaranteed not to resize while accumulating bodies for the solving phase (it will assert if it does)
	 * so pointers to elements are valid for the duration of the constraint solving phase, but not longer.
	 * 
	 * \note This container holds all the state of bodies in an island. Dynamics will only appear in one island, 
	 * but kinematics are in multiple and so their state will be duplicated in each island. This is ok - we do 
	 * not update the state of any kinematic bodies in the constraint solvers. We also assume that the number
	 * of kinematics is small compared to the number of dynamics. If this is commonly untrue we may want to 
	 * consider having a separate (global) container of kinematic solver bodies.
	 */
	class FSolverBodyContainer
	{
	public:

		// Clear the bodies array and allocate enough space for at least MaxBodies bodies.
		// @param MaxBodies The number of bodies that will get added to the container. The container asserts if we attempt to add more than this.
		inline void Reset(int MaxBodies)
		{
			SolverBodies.Reset(MaxBodies);
			ParticleToIndexMap.Reset();
		}

		// The number of bodies in the container
		inline int NumItems() const
		{
			return SolverBodies.Num();
		}

		// The maximum number of bodies the container can hold (until Reset() is called again)
		inline int MaxItems() const
		{
			return SolverBodies.Max();
		}

		// Get a pointer to the item at the specified index, or null for INDEX_NONE
		// @param Index The index of the body to return
		inline FSolverBodyAdapter* TryGetItem(int Index)
		{
			if (IsValid(Index))
			{
				return &SolverBodies[Index];
			}
			return nullptr;
		}

		// Get a pointer to the item at the specified index, or null for INDEX_NONE
		// @param Index The index of the body to return
		inline const FSolverBodyAdapter* TryGetItem(int Index) const
		{
			if (IsValid(Index))
			{
				return &SolverBodies[Index];
			}
			return nullptr;
		}

		// Get a reference to the item at the specified index. Asserts on invalid index.
		// @param Index The index of the body to return
		inline FSolverBodyAdapter& GetItem(int Index)
		{
			return SolverBodies[Index];
		}

		// Get a reference to the item at the specified index. Asserts on invalid index.
		// @param Index The index of the body to return
		inline const FSolverBodyAdapter& GetItem(int Index) const
		{
			return SolverBodies[Index];
		}

		/** Return all the solver bodies inside the container */
		inline TArray<FSolverBodyAdapter>& GetBodies()
		{
			return SolverBodies;
		}

		// Whether the specified index is valid.
		// @param Index The index of the body to return
		inline bool IsValid(int Index) const
		{
			return SolverBodies.IsValidIndex(Index);
		}

		// Add a solver body to represent the solver state of the particle
		// This should ideally be called in the order in which the bodies will be accessed
		// (or as close as we can get, given most constraints access 2 bodies so there is no perfect order)
		FSolverBody* FindOrAdd(FGenericParticleHandle InParticle);

		// Collect all the data we need from the particles represented by our SolverBodies
		//void GatherInput();

		// Scatter the solver results back to the particles represented by our SolverBodies
		void ScatterOutput();

		// Recalculate the velocities of all bodies based on their transform deltas
		void SetImplicitVelocities(FReal Dt);

		// Apply accumulated transform deltas to the body transforms
		void ApplyCorrections();

		// Can be called after ApplyCorrections to update inertia to match the current transform
		void UpdateRotationDependentState();

	private:
		int32 AddParticle(FGenericParticleHandle InParticle);

		// Solver bodies, usually collected in the order in which they are accessed
		TArray<FSolverBodyAdapter> SolverBodies;

		// Used to determine if a (non-dynamic) body is already present in the container. 
		// Dynamic bodies are not in this map - the particle stores its index in this case.
		TMap<FGenericParticleHandle, int32> ParticleToIndexMap;
	};

}
