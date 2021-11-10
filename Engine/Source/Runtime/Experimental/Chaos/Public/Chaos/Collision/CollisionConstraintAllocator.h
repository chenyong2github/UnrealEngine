// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"
#include "ChaosStats.h"

extern bool bChaos_Collision_EnableManifoldRestore;

namespace Chaos
{
	class FParticlePairCollisionsKey
	{
	public:
		FParticlePairCollisionsKey()
			: Key(0)
		{
		}

		FParticlePairCollisionsKey(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
			: Key(GenerateHash(Particle0, Particle1))
		{
		}

		uint32 GetKey() const
		{
			return Key;
		}

	private:
		uint32 GenerateHash(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
		{
			uint32 Hash0 = HashCombine(::GetTypeHash(Particle0->ParticleID().GlobalID), ::GetTypeHash(Particle0->ParticleID().LocalID));
			uint32 Hash1 = HashCombine(::GetTypeHash(Particle1->ParticleID().GlobalID), ::GetTypeHash(Particle1->ParticleID().LocalID));
			return HashCombine(Hash0, Hash1);
		}

		uint32 Key;
	};

	/**
	 * @brief All the contacts on a particle pair
	*/
	class FParticlePairCollisionConstraints
	{
	public:
		FParticlePairCollisionConstraints(const FParticlePairCollisionsKey& InKey, const FGeometryParticleHandle* InParticle0, const FGeometryParticleHandle* InParticle1, const int32 InEpoch)
			: Key(InKey)
			, Particle0(InParticle0)
			, Particle1(InParticle1)
			, CreationEpoch(InEpoch)
			, Epoch(InEpoch)
		{
		}
		
		FParticlePairCollisionsKey GetKey() const
		{
			return Key;
		}

		/**
		 * @brief Whether the particle pair was just created
		*/
		bool IsNew() const
		{
			return CreationEpoch == Epoch;
		}

		/**
		 * @brief The transform of Particle0 when the contacts were created
		*/
		const FRigidTransform3& GetParticle0Transform() const
		{
			return ParticleTransforms[0];
		}

		/**
		 * @brief The transform of Particle1 when the contacts were created
		*/
		const FRigidTransform3& GetParticle1Transform() const
		{
			return ParticleTransforms[1];
		}

		/**
		 * @brief Clear the constraint list and update the transforms (subsequently added contacts will have been created at these transforms)
		*/
		void Reset(int32 InEpoch, const FRigidTransform3& InParticleTransform0, const FRigidTransform3& InParticleTransform1)
		{
			CreationEpoch = Epoch;
			Epoch = InEpoch;
			ParticleTransforms[0] =  InParticleTransform0;
			ParticleTransforms[1] = InParticleTransform1;
			Constraints.Reset();
		}

		/**
		 * @brief Update the epoch counter used for pruning unused pairs
		*/
		void Restored(const int32 InEpoch)
		{
			Epoch = InEpoch;
			Constraints.Reset();
		}

		/**
		 * @brief Get the epoch counter used for pruning unused pairs
		*/
		int32 GetEpoch() const
		{
			return Epoch;
		}

		/**
		 * @brief Whether we can add mroe contacts for this particle pair
		 * This is to limit contact creation when two partiicles have a large number of overlapping shape pairs
		 * @todo(chaos): implement particle pair contact count limit
		*/
		bool IsAcceptingContacts() const
		{
			return true;
		}

		/**
		 * @brief The list of contacts between the particle pair
		*/
		TArrayView<FPBDCollisionConstraint* const> GetConstraints() const
		{
			return MakeArrayView(Constraints);
		}

		/**
		 * @brief Add a constraint to the particle-pair constraint list
		*/
		void AddConstraint(FPBDCollisionConstraint* InContact)
		{
			Constraints.Add(InContact);
		}

		/**
		 * @brief Remove a contact for the particle-pair
		*/
		void RemoveConstraint(FPBDCollisionConstraint* InContact)
		{
			Constraints.Remove(InContact);
		}

		/**
		 * @brief Find the contact for the shape pair
		 * @note O(N) search
		*/
		FPBDCollisionConstraint* FindContact(const FRigidBodyContactKey& ConstraintKey)
		{
			for (FPBDCollisionConstraint* Constraint : Constraints)
			{
				if (Constraint->GetKey() == ConstraintKey)
				{
					return Constraint;
				}
			}
			return nullptr;
		}

	private:
		FParticlePairCollisionsKey Key;
		const FGeometryParticleHandle* Particle0;
		const FGeometryParticleHandle* Particle1;

		// The transforms of the particles when the contacts were created
		FRigidTransform3 ParticleTransforms[2];

		// The contacts between the particle pairs
		TArray<FPBDCollisionConstraint*, TInlineAllocator<1>> Constraints;

		// The tick counter when the particle pair was first created
		int32 CreationEpoch;

		// The tick counter when the contacts were last used (for pruning)
		int32 Epoch;
	};


	/**
	 * @brief An allocator and container of collision constraints that supports reuse of constraints from the previous tick
	 * 
	 * This allocator maintains the following structures
	 * - the set of all allocated constraints
	 * - the set of all active constraints (created or restored in the current tick)
	 * - a mapping from shape pair to active constraints
	 * - a mapping from particle pair to a list of active constraints
	 * 
	 * Constraints are allocated during the collision detection phase and retained between ticks. An attempt ot create
	 * a constraint for the same shape pair as seen on the previous tick will return the existing collision constraint, with
	 * all of its data intact.
	 *
	 * Lists are pruned at the end of each tick so if particles are destroyed or a particle pair is no longer overlapping
	 * the entries will be removed. However, this means that the allocator may briefly contain contacts from a prior
	 * tick for particles that have been destroyed. For this reason, no contacts should be accessed unless they
	 * have been created/restored on the current tick (i.e., the Epoch counter is up to date).
	 * 
	 * All constraint pointers returned are persistent in memory until Destroy() is called, or until they are pruned.
	 * 
	 * @todo(chaos): optimizations. We can probably remove the shape pair map, if it is cheap enough to find the shape pair
	 * contact in the particle pair contact list.
	 * 
	 * @todo(chaos): handle shape or particle pairs being in the opposite order - should be treated the same here
	*/
	class CHAOS_API FCollisionConstraintAllocator
	{
	public:
		FCollisionConstraintAllocator()
			: ShapePairConstraintMap()
			, Constraints()
			, Epoch(0)
			, bInCollisionDetectionPhase(false)
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
			return MakeArrayView(Constraints);
		}

		/**
		 * @brief The set of sweep collision constraints for the current tick (created or reinstated)
		 * 
		 * @note Some elements may be null (constraints that have been deleted)
		 * @note This is not thread-safe and should not be used during the collision detection phase (i.e., when the list is being built)
		*/
		TArrayView<FPBDCollisionConstraint* const> GetSweptConstraints() const
		{ 
			return MakeArrayView(SweptConstraints);
		}

		/**
		 * @brief The set of collision constraints for the current tick (created or reinstated)
		 * 
		 * @note Some elements may be null (constraints that have been deleted)
		 * @note This is not thread-safe and should not be used during the collision detection phase (i.e., when the list is being built)
		 */
		TArrayView<const FPBDCollisionConstraint* const> GetConstConstraints() const
		{
			return MakeArrayView(Constraints);
		}

		/**
		 * @brief Destroy all constraints
		*/
		void Reset()
		{
			Constraints.Reset();
			SweptConstraints.Reset();

			for (auto& KeyValuePair : ShapePairConstraintMap)
			{
				FreeConstraint(KeyValuePair.Value);
			}

			ShapePairConstraintMap.Reset();

			for (auto& KeyValuePair : ParticlePairConstraintMap)
			{
				delete KeyValuePair.Value;
			}

			ParticlePairConstraintMap.Reset();
		}

		/**
		 * @brief Called at the start of the frame to clear the frame's active collision list.
		 * @todo(chaos): This is only required because of the way events work (see AdvanceOneTimeStepTask::DoWork)
		*/
		void BeginFrame()
		{
			Constraints.Reset();
		}

		/**
		 * @brief Called at the start of the tick to prepare for collision detection.
		 * Resets the list of active contacts.
		*/
		void BeginDetectCollisions()
		{
			check(!bInCollisionDetectionPhase);
			bInCollisionDetectionPhase = true;

			// Clear the collision list for this tick - we are about to rebuild it
			Constraints.Reset();
			SweptConstraints.Reset();

			// Update the tick counter
			// NOTE: We do this here rather than in EndDetectionCollisions so that any contacts injected
			// before collision detection count as the previous frame's collisions, e.g., from Islands
			// that are manually awoken by modifying a particle on the game thread. This also needs to be
			// done where we reset the Constraints array so that we can tell we have a valid index from
			// the Epoch.
			++Epoch;

			// Note: we do not reset the shape-pair or particle-pair maps. They may be used to reinstate or reuse
			// constraints. Any constraints that are not reactivated (because the pair is no longer overlapping) 
			// will be pruned at the end of the tick.
		}

		/**
		 * @brief Called after collision detection to clean up
		 * Prunes unused contacts
		*/
		void EndDetectCollisions()
		{
			check(bInCollisionDetectionPhase);
			bInCollisionDetectionPhase = false;

			ProcessNewConstraints();

			PruneShapePairs();

			PruneParticlePairs();
		}

		/**
		 * @brief If we add new constraints after collision detection, do what needs to be done to add them to the system
		 * @note this may destroy the constraints if beyond the cull distance.
		*/
		void ProcessInjectedConstraints()
		{
			ProcessNewConstraints();
		}

		/**
		 * @brief Create a constraint for the specified particle+shape pair, or retrieve the one from the last tick if it exists
		 * This is called by the mid phase, once we have determined that two shapes may collide.
		*/
		FPBDCollisionConstraint* FindOrCreateConstraint(FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1) const
		{
			// See if we already have a contact for these shapes
			const FRigidBodyContactKey Key = FRigidBodyContactKey(Particle0, Implicit0, Simplicial0, Particle1, Implicit1, Simplicial1);
			FPBDCollisionConstraint* Constraint = FindConstraint(Key);
			
			// If we did not find the contact, create and register a new one
			if (Constraint == nullptr)
			{
				Constraint = CreateConstraint(Key, Particle0, Implicit0, Simplicial0, Particle1, Implicit1, Simplicial1);
			}

			// Enqueue constraint to add it to maps etc
			EnqueueNewConstraint(Constraint);

			return Constraint;
		}

		/**
		 * @brief Add a set of pre-built constraints and build required internal mapping data
		 * This is used by the resim cache when restoring constraints after a desync
		 * @note This is called after the main collision detection phase, so it directly performs all the work
		 * that would normally be queued up with a call to EnqueueCollision.
		*/
		void AddResimConstraints(const TArray<FPBDCollisionConstraint>& InConstraints)
		{
			for (const FPBDCollisionConstraint& SourceConstraint : InConstraints)
			{
				FPBDCollisionConstraint* Constraint = FindConstraint(SourceConstraint.GetContainerCookie().Key);
				if (Constraint != nullptr)
				{
					// Copy the constraint over the existing one, taking care to leave the cookie intact (which contains the array index)
					const FPBDCollisionConstraintContainerCookie Cookie = Constraint->GetContainerCookie();

					*Constraint = SourceConstraint;

					Constraint->GetContainerCookie() = Cookie;
				}
				else
				{
					// Create a new constraint and copy the source data
					Constraint = CreateConstraint(SourceConstraint);
				}

				// If this is a new constraint, or was already created but not reused this frame, activate it
				if (Constraint->GetContainerCookie().LastUsedEpoch != Epoch)
				{
					AddConstraintImp(Constraint);
				}
			}
		}

		/**
		 * @brief Destroy the specified constraint
		*/
		void DestroyConstraint(FPBDCollisionConstraint* Constraint)
		{
			DestroyConstraintImp(Constraint);
		}

		/**
		 * @brief Return the set of contact constraints between the specified particle pair, or null if we have not created any yet.
		*/
		FParticlePairCollisionConstraints* FindParticlePairConstraints(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
		{
			const FParticlePairCollisionsKey ParticlePairKey = FParticlePairCollisionsKey(Particle0, Particle1);
			return FindParticlePairConstraints(ParticlePairKey);
		}

		/**
		 * @brief Reinstate the contact constraints for the particle pair
		 * This will only be called when the particle have not moved relative to each other.
		 * @note the actual restoration is deferred until after collision detection to prevent lock contention
		*/
		void RestoreParticlePairConstraints(FParticlePairCollisionConstraints* ParticlePairConstraints)
		{
			if (ParticlePairConstraints != nullptr)
			{
				for (FPBDCollisionConstraint* Constraint : ParticlePairConstraints->GetConstraints())
				{
					// Restore the manifold
					Constraint->RestoreManifold();
				}

				// Enqueue for adding to active array etc
				EnqueueNewConstraints(ParticlePairConstraints->GetConstraints());

				// Reset the active collision list for the particle pair - it gets rebuilt
				ParticlePairConstraints->Restored(Epoch);
			}
		}

		/**
		 * @brief Clear the particle pair contact list
		 * This will be called when the particle pair are still close enough to possible collide but have
		 * moved beyond some threshold so we will be recreating the contacts this tick
		*/
		void ResetParticlePairConstraints(FParticlePairCollisionConstraints* ParticlePairConstraints, const FRigidTransform3& ParticleWorldTransform0, const FRigidTransform3& ParticleWorldTransform1)
		{
			if (ParticlePairConstraints != nullptr)
			{
				ParticlePairConstraints->Reset(Epoch, ParticleWorldTransform0, ParticleWorldTransform1);
			}
		}

		/**
		 * @brief Add a constraint to the active list
		 * @param CollisionConstraint collision constraint to be added
		 * This injects a constraint into the active list that was not generated (or restored)
		 * by the collision detection phase. It is used by the Islands to restore contacts
		 * when an island wakes up but could also be used to inject custom or user-created contacts
		 * with a bit of extra work (see comments in the code).
		 */
		void AddConstraint(FPBDCollisionConstraint* Constraint)
		{
			// This is only intended to be called outside the collision detection phase
			check(!bInCollisionDetectionPhase);

			AddConstraintImp(Constraint);
		}
		
		/**
		* @brief Sort all the constraints for better solver stability
		*/
		void SortConstraintsHandles()
		{
			if(Constraints.Num())
			{
				// We need to sort constraints for solver stability
				// We have to use StableSort so that constraints of the same pair stay in the same order
				// Otherwise the order within each pair can change due to where they start out in the array
				// @todo(chaos): we should label each contact (and shape) for things like warm starting GJK
				// and so we could use that label as part of the key
				// and then we could use regular Sort (which is faster)			
				// @todo(chaos): this can be moved to the island and therefoe done in parallel
				Constraints.StableSort(ContactConstraintSortPredicate);
			}
		}

	private:
		FPBDCollisionConstraint* FindConstraint(FRigidBodyContactKey Key) const
		{
			FPBDCollisionConstraint* const * ItemPtr = ShapePairConstraintMap.Find(Key.GetKey());
			if (ItemPtr != nullptr)
			{
				return *ItemPtr;
			}
			return nullptr;
		}

		FPBDCollisionConstraint* CreateConstraint(FRigidBodyContactKey Key, FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1) const
		{
			// TEMP: until we get the block allocator
			FPBDCollisionConstraint* Constraint = new FPBDCollisionConstraint(Particle0, Implicit0, Simplicial0, Particle1, Implicit1, Simplicial1);

			if (Constraint != nullptr)
			{
				// Initalize the constants used by the container to manage the constraint
				Constraint->GetContainerCookie().Init(Key, Epoch);
			}
			return Constraint;
		}

		FPBDCollisionConstraint* CreateConstraint(const FPBDCollisionConstraint& Other)
		{
			// TEMP: until we get the block allocator
			FPBDCollisionConstraint* Constraint = new FPBDCollisionConstraint(Other);
			return Constraint;
		}

		void DestroyConstraintImp(FPBDCollisionConstraint* Constraint)
		{
			const FPBDCollisionConstraintContainerCookie& Cookie = Constraint->GetContainerCookie();
			check(!Cookie.bIsSleeping);

			FParticlePairCollisionConstraints** ParticlePairConstraintsPtr = ParticlePairConstraintMap.Find(FParticlePairCollisionsKey(Constraint->Particle[0], Constraint->Particle[1]).GetKey());
			if (ParticlePairConstraintsPtr != nullptr)
			{
				FParticlePairCollisionConstraints* ParticlePairConstraints = *ParticlePairConstraintsPtr;
				ParticlePairConstraints->RemoveConstraint(Constraint);
			}

			if (Cookie.bIsInShapePairMap)
			{
				ShapePairConstraintMap.Remove(Cookie.Key.GetKey());
			}

			if ((Cookie.LastUsedEpoch == Epoch) && (Cookie.ConstraintIndex != INDEX_NONE))
			{
				Constraints[Cookie.ConstraintIndex] = nullptr;
			}

			if ((Constraint->GetType() == ECollisionConstraintType::Swept) && (Cookie.LastUsedEpoch == Epoch) && (Cookie.SweptConstraintIndex != INDEX_NONE))
			{
				SweptConstraints[Cookie.SweptConstraintIndex] = nullptr;
			}

			FreeConstraint(Constraint);
		}

		void FreeConstraint(FPBDCollisionConstraint* Constraint)
		{
			if (Constraint != nullptr)
			{
				delete Constraint;
			}
		}

		FParticlePairCollisionConstraints* FindParticlePairConstraints(const FParticlePairCollisionsKey& ParticlePairKey)
		{
			if (!bChaos_Collision_EnableManifoldRestore)
			{
				return nullptr;
			}

			FParticlePairCollisionConstraints** ParticlePairContactsPtr = ParticlePairConstraintMap.Find(ParticlePairKey.GetKey());
			if (ParticlePairContactsPtr != nullptr)
			{
				return *ParticlePairContactsPtr;
			}

			return nullptr;
		}

		FParticlePairCollisionConstraints* FindOrCreateParticlePairConstraints(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
		{
			if (!bChaos_Collision_EnableManifoldRestore)
			{
				return nullptr;
			}

			FParticlePairCollisionsKey ParticlePairKey = FParticlePairCollisionsKey(Particle0, Particle1);
			FParticlePairCollisionConstraints* ParticlePairConstraints = FindParticlePairConstraints(ParticlePairKey);
			if (ParticlePairConstraints == nullptr)
			{
				ParticlePairConstraints = new FParticlePairCollisionConstraints(ParticlePairKey, Particle0, Particle1, Epoch);

				ParticlePairConstraintMap.Add(ParticlePairConstraints->GetKey().GetKey(), ParticlePairConstraints);
			}

			return ParticlePairConstraints; 
		}

		void ProcessNewConstraints()
		{
			if(!NewConstraints.IsEmpty())
			{
				while (FPBDCollisionConstraint* NewConstraint = DequeueNewConstraint())
				{
					// Destroy any contacts beyond the cull distance
					// @todo(chaos): this should not be necessary - we should not create/restore them in the first place
					if (NewConstraint->GetPhi() > NewConstraint->GetCullDistance())
					{
						// NOTE: We cannot remove constraints that are part of a sleeping island - they get restored later.
						// In principle we should never see sleeping constraints in the active list, but it can happen
						// if a dynamic particle gets flipped to a kinematic.
						if (!NewConstraint->IsSleeping())
						{
							DestroyConstraint(NewConstraint);
						}
						continue;
					}

					// Add the constraint to the maps and active list (if not already present)
					AddConstraintImp(NewConstraint);
				}
				SortConstraintsHandles();
			}
		}

		void AddConstraintImp(FPBDCollisionConstraint* CollisionConstraint)
		{
			FPBDCollisionConstraintContainerCookie& Cookie = CollisionConstraint->GetContainerCookie();

			// Add the constraint to the active list and update its epoch
			// but only if it has not already been added since we reset the constraints array.
			// We may attempt to add again if an island is awoken by user interaction (for example)
			// where islands get awoken at the start of the tick, before we have cleared the array
			// and updated the epoch.
			// @todo(chaos): this is messy - we should probably have a different path for pre-collision
			// detection awakenings? Either way, it should be an error to try to Add() twice in the same Epoch
			if (Cookie.LastUsedEpoch != Epoch)
			{
				const int32 ConstraintIndex = Constraints.Add(CollisionConstraint);
				Cookie.Update(ConstraintIndex, Epoch);
			}

			// If this is a new contact, we must add it to the particle-shape-collision maps.
			// This will be false when we are reactivating a collision from the previous frame (it would have been pulled from the maps)
			// but true when restoring a sleeping contact or injecting a user-generated contact for example.
			if (!Cookie.bIsInShapePairMap)
			{
				Cookie.bIsInShapePairMap = true;
				ShapePairConstraintMap.Add(Cookie.Key.GetKey(), CollisionConstraint);
			}

			// NOTE: ParticlePairConstraints contains only the constraints that are active this frame, hence it is rebuilt. Alternatively we could
			// remove constraints that aren't refreshed in the pruning phase if that is faster.
			// @todo(chaos): only store the particle pair contact list when we might want to retore it next frame (small movement)
			// @todo(chaos): store this pointer on the constraint, but make sure it gets reset when put to sleep
			FParticlePairCollisionConstraints* ParticlePairConstraints = FindOrCreateParticlePairConstraints(CollisionConstraint->Particle[0], CollisionConstraint->Particle[1]);
			if (ParticlePairConstraints != nullptr)
			{
				ParticlePairConstraints->AddConstraint(CollisionConstraint);
			}

			if (CollisionConstraint->GetType() == ECollisionConstraintType::Swept)
			{
				const int32 SweptConstraintIndex = SweptConstraints.Add(CollisionConstraint);
				Cookie.UpdateSweptIndex(SweptConstraintIndex);
			}
		}

		void PruneShapePairs()
		{
			// @todo(chaos): this should be optimizable but for now just check every contact to see if it is stale
			// Also, we should be able to clean some of this up now that we have persistent islands. Do we even
			// need a persistent global constraint list now? Maybe the pruning should be in the islands now.

			// Destroy contacts that were not used this tick, as long as they are not held in a sleeping island
			TArray<uint32> PrunedKeyList;
			PrunedKeyList.Reserve(ShapePairConstraintMap.Num());
			for (auto& KeyValuePair : ShapePairConstraintMap)
			{
				FPBDCollisionConstraint* Constraint = KeyValuePair.Value;
				
				if (Constraint->GetContainerCookie().LastUsedEpoch < Epoch)
				{
					if(!Constraint->IsSleeping())
					{
						FreeConstraint(KeyValuePair.Value);
					}
					PrunedKeyList.Add(KeyValuePair.Key);
				}
			}

			// Remove destroyed contacts from the map
			for (uint32 Key : PrunedKeyList)
			{
				ShapePairConstraintMap.Remove(Key);
			}
		}

		void PruneParticlePairs()
		{
			if (!bChaos_Collision_EnableManifoldRestore)
			{
				ParticlePairConstraintMap.Reset();
				return;
			}

			TArray<uint32> PrunedKeyList;
			PrunedKeyList.Reserve(ParticlePairConstraintMap.Num());

			for (auto& KeyValuePair : ParticlePairConstraintMap)
			{
				if (KeyValuePair.Value->GetEpoch() < Epoch)
				{
					delete KeyValuePair.Value;

					PrunedKeyList.Add(KeyValuePair.Key);
				}
			}

			for (uint32 Key : PrunedKeyList)
			{
				ParticlePairConstraintMap.Remove(Key);
			}
		}

		void EnqueueNewConstraint(FPBDCollisionConstraint* InConstraint) const
		{
			if (InConstraint != nullptr)
			{
				NewConstraints.Push(InConstraint);
			}
		}

		void EnqueueNewConstraints(TArrayView<FPBDCollisionConstraint* const> InConstraints) const
		{
			for (FPBDCollisionConstraint* Constraint : InConstraints)
			{
				EnqueueNewConstraint(Constraint);
			}
		}

		FPBDCollisionConstraint* DequeueNewConstraint()
		{
			return NewConstraints.Pop();
		}

		using FShapePairConstraintMap = TMap<uint32, FPBDCollisionConstraint*>;
		using FParticlePairConstraintsMap = TMap<uint32, FParticlePairCollisionConstraints*>;

		// All the constraints indexed by particle pair
		// Note: partciles can be deleted so this map may briefly contain collisions for invalid particles
		// however they will be pruned in PruneParticlePairs()
		FParticlePairConstraintsMap ParticlePairConstraintMap;

		// All of the allocated constraints, indexed by shape pair
		// @todo(chaos): consider removing this and just having ParticlePairConstraintMap
		FShapePairConstraintMap ShapePairConstraintMap;

		// The active constraints (added or recovered this tick)
		TArray<FPBDCollisionConstraint*> Constraints;

		// The active sweep constraints (added or recovered this tick)
		TArray<FPBDCollisionConstraint*> SweptConstraints;

		// The current epoch used to track out-of-date contacts. A constraint whose Epoch is
		// older than the current Epoch at the end of the tick was not refreshed this tick.
		int32 Epoch;

		bool bInCollisionDetectionPhase;

		// The set of constraints created or restored this tick during collision detection
		// @todo(chaos): we can eliminate this lock-free structure by having the broadphase supply an array for use per thread
		mutable TLockFreePointerListLIFO<FPBDCollisionConstraint> NewConstraints;
	};

}