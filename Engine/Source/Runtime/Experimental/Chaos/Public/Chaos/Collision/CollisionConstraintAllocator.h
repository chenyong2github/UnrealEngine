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
		}

		/**
		 * @brief Called each tick after the graph is updated to remove unused collisions
		*/
		void PruneExpiredItems()
		{
			PruneExpiredMidPhases();
		}

		/**
		 * @brief Return a midphase for a particle pair.
		 * This wil create a new midphase if the particle pairs were not recently overlapping, or return an
		 * existing one if they were.
		 * @note Nothing outside of thie allocator should hold a pointer to the midphase, or any constraints 
		 * it creates for more than the duration of the tick. Except the IslandManager :| 
		*/
		FParticlePairMidPhase* GetParticlePairMidPhase(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1, FGeometryParticleHandle* SearchParticlePerformanceHint)
		{
			// NOTE: Called from the CollisionDetection parellel-for loop.

			FParticlePairMidPhase* MidPhase = FindParticlePairMidPhaseImpl(Particle0, Particle1, SearchParticlePerformanceHint);
			if (MidPhase == nullptr)
			{
				MidPhase = CreateParticlePairMidPhase(Particle0, Particle1);
			}

			return MidPhase;
		}

		/**
		 * @brief Return a midphase for a particle pair only if it already exists
		*/
		FParticlePairMidPhase* FindParticlePairMidPhase(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1, FGeometryParticleHandle* SearchParticlePerformanceHint)
		{
			return FindParticlePairMidPhaseImpl(Particle0, Particle1, SearchParticlePerformanceHint);
		}

		/**
		 * @brief Called each tick when a constraint should be processed on that tick (i.e., the shapes are within CullDistance of each other)
		*/
		bool ActivateConstraint(FPBDCollisionConstraint* Constraint)
		{
			// NOTE: Called from the CollisionDetection parellel-for loop.
			// We need to lock the arrays, but we can freely read/write to the constraint without the lock
			// because each constraint is only processed once and not accessed by any other collision detection threads

			// When we wake an Island, we reactivate all constraints for all dynamic particles in the island. This
			// results in duplicate calls to active for constraints involving two dynamic particles, hence the check for CurrentEpoch.
			// @todo(chaos): fix duplicate calls from island wake. See UpdateSleepState in IslandManager.cpp
			FPBDCollisionConstraintContainerCookie& Cookie = Constraint->GetContainerCookie();
			if (Cookie.LastUsedEpoch != CurrentEpoch)
			{
				Cookie.LastUsedEpoch = CurrentEpoch;

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

				FParticlePairMidPhase* MidPhase = GetParticlePairMidPhase(Particle0, Particle1, SourceConstraint.Particle[0]);

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
			// We will be removing collisions, and don't want to have to prune the queues
			check(!bInCollisionDetectionPhase);

			// Loop over all particle pairs involving this particle.
			// Tell each Particle Pair MidPhase that one of its particles is gone. 
			// It will get pruned at the next collision detection phase.
			Particle->ParticleCollisions().VisitMidPhases([Particle](FParticlePairMidPhase& MidPhase)
				{
					MidPhase.DetachParticle(Particle);
					return ECollisionVisitorResult::Continue;
				});
		}

		/**
		 * @brief Iterate over all collisions, including sleeping ones
		*/
		template<typename TLambda>
		void VisitConstCollisions(const TLambda& Visitor) const
		{
			for (const auto& MidPhase : ParticlePairMidPhases)
			{
				if (MidPhase->VisitConstCollisions(Visitor) == ECollisionVisitorResult::Stop)
				{
					return;
				}
			}
		}
		
	private:

		void ProcessNewItems()
		{
			ProcessNewParticlePairMidPhases();
			ProcessNewConstraints();
		}

		FParticlePairMidPhase* FindParticlePairMidPhaseImpl(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1, FGeometryParticleHandle* SearchParticle)
		{
			// Find the existing midphase from one of the particle's lists of midphases
//			FGeometryParticleHandle* SearchParticle2 = (Particle0->ParticleCollisions().Num() <= Particle1->ParticleCollisions().Num()) ? Particle0 : Particle1;

			const FCollisionParticlePairKey Key = FCollisionParticlePairKey(Particle0, Particle1);
			return SearchParticle->ParticleCollisions().FindMidPhase(Key.GetKey());
		}

		FParticlePairMidPhase* CreateParticlePairMidPhase(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1)
		{
			const FCollisionParticlePairKey Key = FCollisionParticlePairKey(Particle0, Particle1);

			// We enqueue a raw pointer and wrap it in a UniquePtr later
			FParticlePairMidPhase* MidPhase = new FParticlePairMidPhase();
			MidPhase->Init(Particle0, Particle1, Key, *this);

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

				ParticlePairMidPhases.Emplace(TUniquePtr<FParticlePairMidPhase>(MidPhase));
				Particle0->ParticleCollisions().AddMidPhase(Particle0, MidPhase);
				Particle1->ParticleCollisions().AddMidPhase(Particle1, MidPhase);
			}
		}

		void DetachParticlePairMidPhase(FParticlePairMidPhase* MidPhase)
		{
			// Remove ParticlePairMidPhase from each Particles list of collisions
			// NOTE: One or both particles may have been destroyed in which case it will
			// have been set to null on the midphase.
			if (FGeometryParticleHandle* Particle0 = MidPhase->GetParticle0())
			{
				Particle0->ParticleCollisions().RemoveMidPhase(Particle0, MidPhase);
			}

			if (FGeometryParticleHandle* Particle1 = MidPhase->GetParticle1())
			{
				Particle1->ParticleCollisions().RemoveMidPhase(Particle1, MidPhase);
			}
		}

		void PruneExpiredMidPhases()
		{
			check(NewParticlePairMidPhases.IsEmpty());
			
			// NOTE: Called from the physics thread. No need for locks.

			// Determine which particle pairs are no longer overlapping
			// Prune all pairs which were not updated this tick as part of the collision
			// detection loop and are not asleep
			int32 NumParticlePairMidPhases = ParticlePairMidPhases.Num();
			for (int32 Index = 0; Index < NumParticlePairMidPhases; ++Index)
			{
				TUniquePtr<FParticlePairMidPhase>& MidPhase = ParticlePairMidPhases[Index];

				// We could also add !MidPhase->IsInConstraintGraph() here, but we know that we will not be in the graph if we were
				// not active this tick and were not asleep. The constraint graph ejects all non-sleeping constraints each tick.
				// (There is a check in the collision destructor that verified this).
				if ((MidPhase != nullptr) && !MidPhase->IsUsedSince(CurrentEpoch) && !MidPhase->IsSleeping())
				{
					// Remove from the particles' lists of contacts
					DetachParticlePairMidPhase(MidPhase.Get());

					// Destroy the midphase. ParticlePairMidPhases can get large so we allow it to shrink from time to time
					const int32 MaxSlack = 1000;
					const int32 Slack = ParticlePairMidPhases.Max() - ParticlePairMidPhases.Num();
					const bool bAllowShrink = (Slack > MaxSlack);
					ParticlePairMidPhases.RemoveAtSwap(Index, 1, bAllowShrink);

					--Index;
					--NumParticlePairMidPhases;
				}
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
		TArray<TUniquePtr<FParticlePairMidPhase>> ParticlePairMidPhases;

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


	///////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////////////
	//
	// Below here is for code that is moved to avoid circular header dependencies
	// See ParticlePairMidPhase.h and ParticleCollisions.h
	//
	///////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////////////

	template<typename TLambda>
	inline ECollisionVisitorResult FMultiShapePairCollisionDetector::VisitCollisions(const int32 LastEpoch, const TLambda& Visitor, const bool bOnlyActive)
	{
		for (auto& KVP : Constraints)
		{
			const TUniquePtr<FPBDCollisionConstraint>& Constraint = KVP.Value;

			// If we only want active constraints, check the timestamp
			if ((Constraint != nullptr) && (!bOnlyActive || (Constraint->GetContainerCookie().LastUsedEpoch >= LastEpoch)))
			{
				if (Visitor(*Constraint) == ECollisionVisitorResult::Stop)
				{
					return ECollisionVisitorResult::Stop;
				}
			}
		}
		return ECollisionVisitorResult::Continue;
	}

	template<typename TLambda>
	inline ECollisionVisitorResult FMultiShapePairCollisionDetector::VisitConstCollisions(const int32 LastEpoch, const TLambda& Visitor, const bool bOnlyActive) const
	{
		for (auto& KVP : Constraints)
		{
			const TUniquePtr<FPBDCollisionConstraint>& Constraint = KVP.Value;

			// If we only want active constraints, check the timestamp
			if ((Constraint != nullptr) && (!bOnlyActive || (Constraint->GetContainerCookie().LastUsedEpoch >= LastEpoch)))
			{
				if (!bOnlyActive || (Visitor(*Constraint) == ECollisionVisitorResult::Stop))
				{
					return ECollisionVisitorResult::Stop;
				}
			}
		}
		return ECollisionVisitorResult::Continue;
	}

	template<typename TLambda>
	inline ECollisionVisitorResult FParticlePairMidPhase::VisitCollisions(const TLambda& Visitor, const bool bOnlyActive)
	{
		const int32 LastEpoch = IsSleeping() ? LastUsedEpoch : GetCurrentEpoch();
		for (FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
		{
			// If we only want active constraints, check the timestamp
			if ((ShapePair.GetConstraint() != nullptr) && (!bOnlyActive || ShapePair.IsUsedSince(LastEpoch)))
			{
				if (Visitor(*ShapePair.GetConstraint()) == ECollisionVisitorResult::Stop)
				{
					return ECollisionVisitorResult::Stop;
				}
			}
		}

		for (FMultiShapePairCollisionDetector& MultiShapePair : MultiShapePairDetectors)
		{
			if (MultiShapePair.VisitCollisions(LastEpoch, Visitor, bOnlyActive) == ECollisionVisitorResult::Stop)
			{
				return ECollisionVisitorResult::Stop;
			}
		}

		return ECollisionVisitorResult::Continue;
	}


	template<typename TLambda>
	inline ECollisionVisitorResult FParticlePairMidPhase::VisitConstCollisions(const TLambda& Visitor, const bool bOnlyActive) const
	{
		const int32 LastEpoch = IsSleeping() ? LastUsedEpoch : GetCurrentEpoch();
		for (const FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
		{
			// If we only want active constraints, check the timestamp
			if ((ShapePair.GetConstraint() != nullptr) && (!bOnlyActive || ShapePair.IsUsedSince(LastEpoch)))
			{
				if (Visitor(*ShapePair.GetConstraint()) == ECollisionVisitorResult::Stop)
				{
					return ECollisionVisitorResult::Stop;
				}
			}
		}

		for (const FMultiShapePairCollisionDetector& MultiShapePair : MultiShapePairDetectors)
		{
			if (MultiShapePair.VisitConstCollisions(LastEpoch, Visitor, bOnlyActive) == ECollisionVisitorResult::Stop)
			{
				return ECollisionVisitorResult::Stop;
			}
		}

		return ECollisionVisitorResult::Continue;
	}


	template<typename TLambda>
	inline ECollisionVisitorResult FParticleCollisions::VisitMidPhases(const TLambda& Lambda)
	{
		for (int32 Index = 0; Index < MidPhases.Num(); ++Index)
		{
			if (Lambda(*MidPhases[Index].Value) == ECollisionVisitorResult::Stop)
			{
				return ECollisionVisitorResult::Stop;
			}
		}
		return ECollisionVisitorResult::Continue;
	}

	template<typename TLambda>
	inline ECollisionVisitorResult FParticleCollisions::VisitConstMidPhases(const TLambda& Lambda) const
	{
		for (int32 Index = 0; Index < MidPhases.Num(); ++Index)
		{
			if (Lambda(*MidPhases[Index].Value) == ECollisionVisitorResult::Stop)
			{
				return ECollisionVisitorResult::Stop;
			}
		}
		return ECollisionVisitorResult::Continue;
	}

	template<typename TLambda>
	inline ECollisionVisitorResult FParticleCollisions::VisitCollisions(const TLambda& Visitor)
	{
		return VisitMidPhases([&Visitor](FParticlePairMidPhase& MidPhase)
			{
				if (MidPhase.VisitCollisions(Visitor) == ECollisionVisitorResult::Stop)
				{
					return ECollisionVisitorResult::Stop;
				}
				return ECollisionVisitorResult::Continue;
			});
	}

	template<typename TLambda>
	inline ECollisionVisitorResult FParticleCollisions::VisitConstCollisions(const TLambda& Visitor) const
	{
		return VisitConstMidPhases([&Visitor](const FParticlePairMidPhase& MidPhase)
			{
				if (MidPhase.VisitConstCollisions(Visitor) == ECollisionVisitorResult::Stop)
				{
					return ECollisionVisitorResult::Stop;
				}
				return ECollisionVisitorResult::Continue;
			});
	}


}
