// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionKeys.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/ParticlePairMidPhase.h"
#include "Chaos/ParticleHandle.h"
#include "ChaosStats.h"

extern bool bChaos_Collision_EnableManifoldRestore;

namespace Chaos
{
	/**
	 * @brief An allocator and container of collision constraints that supports reuse of constraints from the previous tick
	 * 
	 * All constraint pointers are persistent in memory until Destroy() is called, or until they are pruned.

	 * This allocator maintains the set of all overlapping particle pairs, with each overlapping particle pair managed
	 * by a FParticlePairMidPhase object. the MidPhase object is what actually calls the Narrow Phase and maintains
	 * the set of collision constraints for all the shape pairs on the particles.
	 * 
	 * Constraints are allocated during the collision detection phase and retained between ticks. An attempt ot create
	 * a constraint for the same shape pair as seen on the previous tick will return the existing collision constraint, with
	 * all of its data intact.
	 *
	 * The allocator also keeps a list of Standard and Swept collision constraints that are active for the current tick.
	 * This list gets reset and rebuilt every frame during collision detection. It may get added to by the IslandManager
	 * if some islands are woken following collision detection.
	 * 
	 * The allocators Epoch counter is used to determine whether a constraint (or midphase object) generated any
	 * contacts for the current frame. When a midphase creates or updates a constraint, it copies the current Epoch
	 * counter.
	 * 
	 * The Midphase list is pruned at the end of each tick so if particles are destroyed or a particle pair is no longer 
	 * overlapping.
	 * 
	*/
	class CHAOS_API FCollisionConstraintAllocator
	{
	public:
		FCollisionConstraintAllocator()
			: ParticlePairMidPhases()
			, ActiveConstraints()
			, ActiveSweptConstraints()
			, CurrentEpoch(0)
			, bInCollisionDetectionPhase(false)
		{
		}

		~FCollisionConstraintAllocator()
		{
		}
		
		/**
		 * @brief The set of collision constraints for the current tick (created or reinstated)
		 * 
		 * @note Some elements may be null (constraints that have been deleted)
		 * @note This is not thread-safe and should not be used during the collision detection phase (i.e., when the list is being built)
		*/
		TArrayView<FPBDCollisionConstraint* const> GetConstraints() const
		{ 
			return MakeArrayView(ActiveConstraints);
		}

		/**
		 * @brief The set of sweep collision constraints for the current tick (created or reinstated)
		 * 
		 * @note Some elements may be null (constraints that have been explicitly deleted)
		 * @note This is not thread-safe and should not be used during the collision detection phase (i.e., when the list is being built)
		*/
		TArrayView<FPBDCollisionConstraint* const> GetSweptConstraints() const
		{ 
			return MakeArrayView(ActiveSweptConstraints);
		}

		/**
		 * @brief The set of collision constraints for the current tick (created or reinstated)
		 * 
		 * @note Some elements may be null (constraints that have been explicitly deleted)
		 * @note This is not thread-safe and should not be used during the collision detection phase (i.e., when the list is being built)
		 */
		TArrayView<const FPBDCollisionConstraint* const> GetConstConstraints() const
		{
			return MakeArrayView(ActiveConstraints);
		}

		int32 GetCurrentEpoch() const
		{
			return CurrentEpoch;
		}

		/**
		 * @brief Destroy all constraints
		*/
		void Reset()
		{
			ActiveConstraints.Reset();
			ActiveSweptConstraints.Reset();
			ParticlePairMidPhases.Reset();
		}

		/**
		 * @brief Called at the start of the frame to clear the frame's active collision list.
		 * @todo(chaos): This is only required because of the way events work (see AdvanceOneTimeStepTask::DoWork)
		*/
		void BeginFrame()
		{
			ActiveConstraints.Reset();
			ActiveSweptConstraints.Reset();

			// If we hit this we Activated constraints without calling ProcessNewItems
			check(NewParticlePairMidPhases.IsEmpty());
			check(NewConstraints.IsEmpty());
		}

		/**
		 * @brief Called at the start of the tick to prepare for collision detection.
		 * Resets the list of active contacts.
		*/
		void BeginDetectCollisions()
		{
			check(!bInCollisionDetectionPhase);
			bInCollisionDetectionPhase = true;

			// If we hit this we Activated constraints without calling ProcessNewItems
			check(NewParticlePairMidPhases.IsEmpty());
			check(NewConstraints.IsEmpty());

			// Clear the collision list for this tick - we are about to rebuild them
			ActiveConstraints.Reset();
			ActiveSweptConstraints.Reset();

			// Update the tick counter
			// NOTE: We do this here rather than in EndDetectionCollisions so that any contacts injected
			// before collision detection count as the previous frame's collisions, e.g., from Islands
			// that are manually awoken by modifying a particle on the game thread. This also needs to be
			// done where we reset the Constraints array so that we can tell we have a valid index from
			// the Epoch.
			++CurrentEpoch;
		}

		/**
		 * @brief Called after collision detection to clean up
		 * Prunes unused contacts
		*/
		void EndDetectCollisions()
		{
			check(bInCollisionDetectionPhase);
			bInCollisionDetectionPhase = false;

			ProcessNewItems();

			PruneExpiredItems();
		}

		/**
		 * @brief Return a midphase for a particle pair.
		 * This wil create a new midphase if the particle pairs were not recently overlapping, or return an
		 * existing one if they were.
		 * @note Nothing outside of thie allocator should hold a pointer to the midphase, or any constraints 
		 * it creates for more than the duration of the tick. Except the IslandManager :| 
		*/
		FParticlePairMidPhase* GetParticlePairMidPhase(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1)
		{
			FCollisionParticlePairKey Key = FCollisionParticlePairKey(Particle0, Particle1);

			FParticlePairMidPhase* MidPhase = FindParticlePairMidPhaseImpl(Key);
			if (MidPhase == nullptr)
			{
				MidPhase = CreateParticlePairMidPhase(Particle0, Particle1, Key);
			}

			return MidPhase;
		}

		/**
		 * @brief Return a midphase for a particle pair only if it already exists
		*/
		FParticlePairMidPhase* FindParticlePairMidPhase(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1)
		{
			FCollisionParticlePairKey Key = FCollisionParticlePairKey(Particle0, Particle1);
			return FindParticlePairMidPhaseImpl(Key);
		}

		/**
		 * @brief Called each tick when a constraint should be processed on that tick (i.e., the shapes are within CullDistance of each other)
		*/
		bool ActivateConstraint(FPBDCollisionConstraint* Constraint)
		{
			// When we wake an Island, we reactivate all constraints for all dynamic particles in the island. This
			// results in duplicate calls to active for constraints involving two dynamic particles.
			// @todo(chaos): fix duplicate calls from island wake. See UpdateSleepState in IslandManager.cpp
			if (Constraint->GetContainerCookie().LastUsedEpoch != CurrentEpoch)
			{
				Constraint->GetContainerCookie().LastUsedEpoch = CurrentEpoch;

				NewConstraints.Push(Constraint);

				return true;
			}
			return false;
		}

		/**
		 * @brief If we add new constraints after collision detection, do what needs to be done to add them to the system
		*/
		void ProcessInjectedConstraints()
		{
			ProcessNewItems();
		}

		/**
		 * @brief Add a set of pre-built constraints and build required internal mapping data
		 * This is used by the resim cache when restoring constraints after a desync
		*/
		void AddResimConstraints(const TArray<FPBDCollisionConstraint>& InConstraints)
		{
			for (const FPBDCollisionConstraint& SourceConstraint : InConstraints)
			{
				// We must keep the particles in the same order that the broadphase would generate when
				// finding or creating the midphase. This is because Collision Constraints may have the
				// particles in the opposite order to the midphase tha owns them.
				FGeometryParticleHandle* Particle0 = SourceConstraint.Particle[0];
				FGeometryParticleHandle* Particle1 = SourceConstraint.Particle[1];
				if (ShouldSwapParticleOrder(Particle0, Particle1))
				{
					Swap(Particle0, Particle1);
				}

				FParticlePairMidPhase* MidPhase = GetParticlePairMidPhase(Particle0, Particle1);

				// We may be adding multiple constraints for the same particle pair, so we need
				// to make sure the map is up to date in the case where we just created a new MidPhase
				ProcessNewParticlePairMidPhases();
				
				if (MidPhase != nullptr)
				{
					MidPhase->InjectCollision(SourceConstraint);
				}
			}

			ProcessNewConstraints();
		}

		/**
		* @brief Sort all the constraints for better solver stability
		*/
		void SortConstraintsHandles()
		{
			if(ActiveConstraints.Num())
			{
				// We need to sort constraints for solver stability
				// We have to use StableSort so that constraints of the same pair stay in the same order
				// Otherwise the order within each pair can change due to where they start out in the array
				// @todo(chaos): we should label each contact (and shape) for things like warm starting GJK
				// and so we could use that label as part of the key
				// and then we could use regular Sort (which is faster)			
				// @todo(chaos): this can be moved to the island and therefoe done in parallel
				ActiveConstraints.StableSort(ContactConstraintSortPredicate);
			}
		}

		/**
		 * @brief Destroy all collision and caches involving the particle
		 * Called when a particle is destroyed or disabled (not sleeping).
		*/
		void RemoveParticle(FGeometryParticleHandle* Particle)
		{
			// We will be removing collisions, and don't want to have to prune the NewConstraints queue
			check(!bInCollisionDetectionPhase);

			// Lop over all particle pairs involving this particle.
			// Tell each Particle Pair MidPhase that one of its particles is gone. 
			// It will get pruned at the next collision detection phase.
			FParticleCollisions& ParticleCollisions = Particle->ParticleCollisions();
			for (FParticlePairMidPhase* MidPhase : ParticleCollisions.GetParticlePairs())
			{
				MidPhase->DetachParticle(Particle);
			}
		}

		/**
		 * @brief Iterate over all collisions, including sleeping ones
		*/
		void VisitCollisions(const TFunction<void(const FPBDCollisionConstraint*)>& Visitor) const
		{
			for (const auto& KVP : ParticlePairMidPhases)
			{
				const TUniquePtr<FParticlePairMidPhase>& MidPhase = KVP.Value;
				MidPhase->VisitCollisions(Visitor);
			}
		}

	private:

		void ProcessNewItems()
		{
			ProcessNewParticlePairMidPhases();
			ProcessNewConstraints();
		}

		void PruneExpiredItems()
		{
			PruneExpiredMidPhases();
		}

		FParticlePairMidPhase* FindParticlePairMidPhaseImpl(const FCollisionParticlePairKey& Key)
		{
			TUniquePtr<FParticlePairMidPhase>* ExistingMidPhase = ParticlePairMidPhases.Find(Key.GetKey());
			if (ExistingMidPhase != nullptr)
			{
				return (*ExistingMidPhase).Get();
			}
			return nullptr;
		}

		FParticlePairMidPhase* CreateParticlePairMidPhase(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1, const FCollisionParticlePairKey& Key)
		{
			// We enqueue a raw pointer and wrap it in a UniquePtr later
			FParticlePairMidPhase* MidPhase = new FParticlePairMidPhase(Particle0, Particle1, Key, *this);

			NewParticlePairMidPhases.Push(MidPhase);

			return MidPhase;
		}

		void ProcessNewParticlePairMidPhases()
		{
			while (FParticlePairMidPhase* MidPhase = NewParticlePairMidPhases.Pop())
			{
				FGeometryParticleHandle* Particle0 = MidPhase->GetParticle0();
				FGeometryParticleHandle* Particle1 = MidPhase->GetParticle1();

				FCollisionParticlePairKey Key = FCollisionParticlePairKey(Particle0, Particle1);

				ParticlePairMidPhases.Add(Key.GetKey(), TUniquePtr<FParticlePairMidPhase>(MidPhase));

				Particle0->ParticleCollisions().AddParticlePair(MidPhase);
				Particle1->ParticleCollisions().AddParticlePair(MidPhase);
			}
		}

		void DestroyParticlePairMidPhase(FParticlePairMidPhase* MidPhase)
		{
			// Remove ParticlePairMidPhase from each Particles list of collisions
			// NOTE: One or both particles may have been destroyed in which case it will
			// have been set to null on the midphase.
			if (FGeometryParticleHandle* Particle0 = MidPhase->GetParticle0())
			{
				Particle0->ParticleCollisions().RemoveParticlePair(MidPhase);
			}

			if (FGeometryParticleHandle* Particle1 = MidPhase->GetParticle1())
			{
				Particle1->ParticleCollisions().RemoveParticlePair(MidPhase);
			}

			// Destroy the midphase and all its constraints
			ParticlePairMidPhases.Remove(MidPhase->GetKey().GetKey());
		}

		void PruneExpiredMidPhases()
		{
			check(NewParticlePairMidPhases.IsEmpty());

			// Determine which particle pairs are no longer overlapping
			// Prune all pairs which wer not updated this tick as part of the collision
			// detection loop and are not asleep
			// @todo(chaos): is there any value in waiting a few frames before destroying?
			TArray<FParticlePairMidPhase*> Pruned;
			for (auto& KVP : ParticlePairMidPhases)
			{
				TUniquePtr<FParticlePairMidPhase>& MidPhase = KVP.Value;
				if ((MidPhase != nullptr) && !MidPhase->IsUsedSince(CurrentEpoch) && !MidPhase->IsSleeping())
				{
					Pruned.Add(MidPhase.Get());
				}
			}

			for (FParticlePairMidPhase* MidPhase : Pruned)
			{
				DestroyParticlePairMidPhase(MidPhase);
			}
		}

		void ProcessNewConstraints()
		{
			if(!NewConstraints.IsEmpty())
			{
				while (FPBDCollisionConstraint* NewConstraint = NewConstraints.Pop())
				{
					// Add the constraint to the active list
					ActivateConstraintImp(NewConstraint);
				}
				SortConstraintsHandles();
			}
		}

		void ActivateConstraintImp(FPBDCollisionConstraint* CollisionConstraint)
		{
			FPBDCollisionConstraintContainerCookie& Cookie = CollisionConstraint->GetContainerCookie();

			// Add the constraint to the active list and update its epoch
			checkSlow(ActiveConstraints.Find(CollisionConstraint) == INDEX_NONE);
			Cookie.ConstraintIndex = ActiveConstraints.Add(CollisionConstraint);

			if (CollisionConstraint->GetCCDType() == ECollisionCCDType::Enabled)
			{
				checkSlow(ActiveSweptConstraints.Find(CollisionConstraint) == INDEX_NONE);
				Cookie.SweptConstraintIndex = ActiveSweptConstraints.Add(CollisionConstraint);
			}

			Cookie.LastUsedEpoch = CurrentEpoch;
		}

		// All of the overlapping particle pairs in the scene
		TMap<uint64, TUniquePtr<FParticlePairMidPhase>> ParticlePairMidPhases;

		// The active constraints (added or recovered this tick)
		TArray<FPBDCollisionConstraint*> ActiveConstraints;

		// The active sweep constraints (added or recovered this tick)
		TArray<FPBDCollisionConstraint*> ActiveSweptConstraints;

		// The current epoch used to track out-of-date contacts. A constraint whose Epoch is
		// older than the current Epoch at the end of the tick was not refreshed this tick.
		int32 CurrentEpoch;

		// For assertions
		bool bInCollisionDetectionPhase;

		// The set of constraints created or restored this tick during collision detection
		mutable TLockFreePointerListLIFO<FPBDCollisionConstraint> NewConstraints;
		
		// The set of mid phases created this tick (i.e., for particle pairs that were not in the map)
		mutable TLockFreePointerListLIFO<FParticlePairMidPhase> NewParticlePairMidPhases;
	};

}