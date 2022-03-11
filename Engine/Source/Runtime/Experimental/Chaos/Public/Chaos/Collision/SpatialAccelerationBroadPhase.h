// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "Chaos/Collision/StatsData.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Capsule.h"
#include "ChaosStats.h"
#include "Chaos/EvolutionResimCache.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/AABBTree.h"

namespace Chaos
{
	template <typename TPayloadType, typename T, int d>
	class ISpatialAcceleration;
	class IResimCacheBase;

	/**
	 *
	 */
	struct FSimOverlapVisitor
	{
		TArray<FAccelerationStructureHandle>& Intersections;
		FSimOverlapVisitor(FGeometryParticleHandle* ParticleHandle, const FCollisionFilterData& InSimFilterData, TArray<FAccelerationStructureHandle>& InIntersections)
			: Intersections(InIntersections)
			, SimFilterData(InSimFilterData)
			, ParticleUniqueIdx(ParticleHandle ? ParticleHandle->UniqueIdx() : FUniqueIdx(0))
			, AccelerationHandle(ParticleHandle)
		{
		}

		bool VisitOverlap(const TSpatialVisitorData<FAccelerationStructureHandle>& Instance)
		{
			Intersections.Add(Instance.Payload);
			return true;
		}

		bool VisitSweep(TSpatialVisitorData<FAccelerationStructureHandle>, FQueryFastData& CurData)
		{
			check(false);
			return false;
		}

		bool VisitRaycast(TSpatialVisitorData<FAccelerationStructureHandle>, FQueryFastData& CurData)
		{
			check(false);
			return false;
		}

		const void* GetQueryData() const { return nullptr; }

		const void* GetSimData() const { return &SimFilterData; }

		bool ShouldIgnore(const TSpatialVisitorData<FAccelerationStructureHandle>& Instance) const
		{
			return Instance.Payload.UniqueIdx() == ParticleUniqueIdx;
		}
		/** Return a pointer to the payload on which we are querying the acceleration structure */
		const void* GetQueryPayload() const
		{
			return &AccelerationHandle;
		}

	private:
		FCollisionFilterData SimFilterData;
		FUniqueIdx ParticleUniqueIdx; // unique id of the particle visiting, used to skip self intersection as early as possible

		/** Handle to be stored to retrieve the payload on which we are querying the acceleration structure*/
		FAccelerationStructureHandle AccelerationHandle;
	};

	/**
	 * A broad phase that iterates over particle and uses a spatial acceleration structure to output
	 * potentially overlapping SpatialAccelerationHandles.
	 */
	class FSpatialAccelerationBroadPhase
	{
	public:
		using FAccelerationStructure = ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>;

		FSpatialAccelerationBroadPhase(const FPBDRigidsSOAs& InParticles)
			: Particles(InParticles)
			, SpatialAcceleration(nullptr)
		{
		}

		void SetSpatialAcceleration(const FAccelerationStructure* InSpatialAcceleration)
		{
			SpatialAcceleration = InSpatialAcceleration;
		}

		/**
		 * Generate all overlapping pairs and pass them to the narrow phase.
		 */
		void ProduceOverlaps(
			FReal Dt, 
			FNarrowPhase& NarrowPhase, 
			IResimCacheBase* ResimCache
			)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_SpatialBroadPhase);

			if (!ensure(SpatialAcceleration))
			{
				// Must call SetSpatialAcceleration
				return;
			}

			if (const auto AABBTree = SpatialAcceleration->template As<TAABBTree<FAccelerationStructureHandle, TAABBTreeLeafArray<FAccelerationStructureHandle>>>())
			{
				ProduceOverlaps(Dt, *AABBTree, NarrowPhase, ResimCache);
			}
			else if (const auto BV = SpatialAcceleration->template As<TBoundingVolume<FAccelerationStructureHandle>>())
			{
				ProduceOverlaps(Dt, *BV, NarrowPhase, ResimCache);
			}
			else if (const auto AABBTreeBV = SpatialAcceleration->template As<TAABBTree<FAccelerationStructureHandle, TBoundingVolume<FAccelerationStructureHandle>>>())
			{
				ProduceOverlaps(Dt, *AABBTreeBV, NarrowPhase, ResimCache);
			}
			else if (const auto Collection = SpatialAcceleration->template As<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>>())
			{
				Collection->PBDComputeConstraintsLowLevel(Dt, *this, NarrowPhase, ResimCache);
			}
			else
			{
				check(false);  //question: do we want to support a dynamic dispatch version?
			}
		}
		
		/** @brief This function is the outer loop of collision detection. It loops over the
		 * particles view and do the broadphase + narrowphase collision detection
		 * @param OverlapView View to consider for the outer loop
		 * @param Dt Current simulation time step
		 * @param InSpatialAcceleration Spatial acceleration (AABB, bounding volumes...) to be used for broadphase collision detection
		 * @param NarrowPhase Narrowphase collision detection that will be executed on each potential pairs coming from the broadphase detection
		 * */

		template<bool bNeedsResim, bool bOnlyRigid, typename ViewType, typename SpatialAccelerationType>
		void ComputeParticlesOverlaps(ViewType& OverlapView, FReal Dt,
			const SpatialAccelerationType& InSpatialAcceleration, FNarrowPhase& NarrowPhase)
		{
			 OverlapView.ParallelFor([&](auto& Particle1,int32 ActiveIdxIdx)
			 {
			 	FGenericParticleHandleImp GenericHandle(Particle1.Handle());
			 	ProduceParticleOverlaps<bNeedsResim,bOnlyRigid>(Dt,GenericHandle, InSpatialAcceleration,NarrowPhase,ActiveIdxIdx);
			},bDisableCollisionParallelFor);
		}

		template<typename T_SPATIALACCELERATION>
		void ProduceOverlaps(
			FReal Dt, 
			const T_SPATIALACCELERATION& InSpatialAcceleration, 
			FNarrowPhase& NarrowPhase, 
			IResimCacheBase* ResimCache
			)
		{
			const bool bDisableParallelFor = bDisableCollisionParallelFor;
			auto* EvolutionResimCache = static_cast<FEvolutionResimCache*>(ResimCache);
			const bool bResimSkipCollision = ResimCache && ResimCache->IsResimming();
			if(!bResimSkipCollision)
			{
				const TParticleView<TPBDRigidParticles<FReal, 3>>& DynamicSleepingView = Particles.GetNonDisabledDynamicView();
				const TParticleView<TKinematicGeometryParticles<FReal, 3>>& DynamicMovingKinematicView = Particles.GetActiveDynamicMovingKinematicParticlesView();

				if(DynamicSleepingView.Num() > DynamicMovingKinematicView.Num())
				{
					ComputeParticlesOverlaps<false,false>(DynamicMovingKinematicView, Dt, InSpatialAcceleration, NarrowPhase);
				}
				else
				{
					ComputeParticlesOverlaps<false,true>(DynamicSleepingView, Dt, InSpatialAcceleration, NarrowPhase);
				}
			}
			else
			{
				const TParticleView<TGeometryParticles<FReal, 3>>& DesyncedView = ResimCache->GetDesyncedView();
				
				ComputeParticlesOverlaps<true,false>(DesyncedView, Dt, InSpatialAcceleration, NarrowPhase);
			}
		}

		FIgnoreCollisionManager& GetIgnoreCollisionManager() { return IgnoreCollisionManager; }

	private:
		template<bool bIsResimming, bool bOnlyRigid, typename THandle, typename T_SPATIALACCELERATION>
		void ProduceParticleOverlaps(
		    FReal Dt,
		    THandle& Particle1,
		    const T_SPATIALACCELERATION& InSpatialAcceleration,
		    FNarrowPhase& NarrowPhase,
			int32 EntryIndex)
		{
			TArray<FAccelerationStructureHandle> PotentialIntersections;  

			const bool bHasValidState = bOnlyRigid ?
					(Particle1.ObjectState() == EObjectStateType::Dynamic || Particle1.ObjectState() == EObjectStateType::Sleeping) :
					(Particle1.ObjectState() == EObjectStateType::Dynamic || Particle1.ObjectState() == EObjectStateType::Kinematic);
			
			if (bIsResimming || bHasValidState)
			{
				const bool bBody1Bounded = HasBoundingBox(Particle1);
				{
					SCOPE_CYCLE_COUNTER(STAT_Collisions_AABBTree);
					if (bBody1Bounded)
					{
						// @todo(chaos): cache this on the particle?
						FCollisionFilterData ParticleSimData;
						FAccelerationStructureHandle::ComputeParticleSimFilterDataFromShapes(Particle1, ParticleSimData);

						const FAABB3 Box1 = Particle1.WorldSpaceInflatedBounds();

						{
							PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, DetectCollisions_BroadPhase);
							FSimOverlapVisitor OverlapVisitor(Particle1.Handle() , ParticleSimData, PotentialIntersections);
							InSpatialAcceleration.Overlap(Box1, OverlapVisitor);
						}
						
					}
					else
					{
						const auto& GlobalElems = InSpatialAcceleration.GlobalObjects();
						PotentialIntersections.Reserve(GlobalElems.Num());

						for (auto& Elem : GlobalElems)
						{
							PotentialIntersections.Add(Elem.Payload);
						}
					}
				}

				SCOPE_CYCLE_COUNTER(STAT_Collisions_Filtering);
				const int32 NumPotentials = PotentialIntersections.Num();
				int32 NumIntoNarrowPhase = 0;
				TArray<FParticlePairMidPhase*> MidPhasePairs;
				MidPhasePairs.Reserve(NumPotentials);
				for (int32 i = 0; i < NumPotentials; ++i)
				{
					auto& Particle2 = *PotentialIntersections[i].GetGeometryParticleHandle_PhysicsThread();
					const FGenericParticleHandle Particle2Generic(&Particle2);

					// Broad Phase Culling
					// CollisionGroup == 0 : Collide_With_Everything
					// CollisionGroup == INDEX_NONE : Disabled collisions
					// CollisionGroup_A != CollisionGroup_B : Skip Check
					
					// collision pair is valid if at least one of the 2 particles is a rigid one (will discard static/kinematic-static/kinematic pairs)
					if(Particle1.CastToRigidParticle() == nullptr && Particle2.CastToRigidParticle() == nullptr)
					{
						continue;
					}
					
					if (Particle1.HasCollisionConstraintFlag(ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions) || 
						Particle2Generic->HasCollisionConstraintFlag(ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions))
					{
						if (IgnoreCollisionManager.IgnoresCollision(Particle1.UniqueIdx(), Particle2.UniqueIdx()))
						{
							continue;
						}
					}					
				
					if (Particle1.CollisionGroup() == INDEX_NONE || Particle2Generic->CollisionGroup() == INDEX_NONE)
					{
						continue;
					}
					if (Particle1.CollisionGroup() && Particle2Generic->CollisionGroup() && Particle1.CollisionGroup() != Particle2Generic->CollisionGroup())
					{
						continue;
					}

					if (!Particle1.Geometry() && !Particle2.Geometry())
					{
						continue;
					}

					if (Particle1.Handle() == Particle2.Handle())
					{
						continue;
					}

					// HACK : This should not be happening if the disabled particles are properly removed from the active particles list. 
					if (Particle1.Disabled() || Particle2Generic->Disabled())
					{
						continue;
					}
					bool bAcceptParticlePair = false;

					// In both cases (resim or not) we will generate (1) dynamic-(sleeping,kinematic(steady+moving),static) pairs + (2) sleeping-moving kinematic ones
					// Sleeping particles could collide with dynamic ones but these collisions are already handled in case 1
					// Sleeping particles won't collide with static or steady kinematic particles since both are not supposed to move but will
					// collide against moving kinematic particles
					if ((Particle1.ObjectState() == EObjectStateType::Dynamic && Particle2.ObjectState() != EObjectStateType::Dynamic) ||
						(Particle1.ObjectState() == EObjectStateType::Sleeping && Particle2.ObjectState() == EObjectStateType::Kinematic))
					{
						bAcceptParticlePair = true;
					}
					
					// Used to determine a winner in cases where we will visit particle pairs in both orders
					const bool bIsParticle1Preferred = (Particle2.ParticleID() < Particle1.ParticleID());
					
					if(!bIsResimming)
					{
						// Normally (not resimming) we iterate over dynamic and asleep|kinematic particles, so:
						// - Particle1 is dynamic, asleep OR kinematic
						// - Particle2 may be static, kinematic, dynamic, asleep

						// If Particle1 is non dynamic but particle 2 is dynamic, the case should already be handled by (1)
						if(Particle1.ObjectState() != EObjectStateType::Dynamic && Particle2.ObjectState() == EObjectStateType::Dynamic)
						{
							bAcceptParticlePair = false;
						}
						// If Particle1 and Particle2 are dynamic we validate the pair if particle1 has higher ID to discard duplicates since we will visit twice the pairs
						else if(Particle1.ObjectState() == EObjectStateType::Dynamic && Particle2.ObjectState() == EObjectStateType::Dynamic)
						{
							bAcceptParticlePair = bIsParticle1Preferred;
						}
						// If Particle1 is kinematic we should in theory discard the pairs against sleeping particles
						// since the sleeping-kinematic case has been validated in (2). But Particle1 is asleep OR kinematic so
						// when entering this condition we are sure that we enver entered (2). It is why we validate the kinematic-sleeping pairs as well
						else if(Particle1.ObjectState() == EObjectStateType::Kinematic && Particle2.ObjectState() == EObjectStateType::Sleeping)
						{
							bAcceptParticlePair = true;
						}
					}
					else
					{
						// When resimming we iterate over "desynced" particles which may be kinematic so:
						// - Particle1 is always desynced
						// - Particle2 may also be desynced, in which case we will also visit the opposite ordering regardless of dynamic/kinematic status
						// - Particle1 may be static, kinematic, dynamic, asleep
						// - Particle2 may be static, kinematic, dynamic, asleep
						// 
						// Even though Particle1 may be kinematic when resimming, we want to create the contacts in the original order (i.e., dynamic first)
						// 
						const bool bIsParticle2Desynced = bIsResimming && (Particle2.SyncState() == ESyncState::HardDesync);

						// If Particle1 is non dynamic but particle 2 is dynamic, the case should already be handled by (1) for
						// the desynced dynamic - synced/desynced (static,kinematic,asleep) pairs. But we still need to process
						// the desynced (static,kinematic,asleep) against the synced dynamic since this case have not been handled by (1)
						if(Particle1.ObjectState() != EObjectStateType::Dynamic && Particle2.ObjectState() == EObjectStateType::Dynamic)
						{
							bAcceptParticlePair = !bIsParticle2Desynced;
						}
						// If Particle1 and Particle2 are dynamic we validate the pair if particle1 has higher ID to discard duplicates since we will visit twice the pairs
						// We validate the pairs as well if the particle 2 is synced since  we will never visit the opposite order (we only iterate over desynced particles)
						else if(Particle1.ObjectState() == EObjectStateType::Dynamic && Particle2.ObjectState() == EObjectStateType::Dynamic)
						{
							bAcceptParticlePair = bIsParticle1Preferred || !bIsParticle2Desynced;
						}
						// If Particle1 is kinematic we are discarding the pairs against sleeping desynced particles since the case has been handled by (2)
						else if(Particle1.ObjectState() == EObjectStateType::Kinematic && Particle2.ObjectState() == EObjectStateType::Sleeping)
						{
							bAcceptParticlePair = !bIsParticle2Desynced;
						}
					}
					
					if (!bAcceptParticlePair)
					{
						continue;
					}

					// Constraints have a key generated from the Particle IDs and we don't want to have to deal with constraint being created from particles in the two orders.
					// Since particles can change type (between Kinematic and Dynamic) we may visit them in different orders at different times, but if we allow
					// that it would break Resim and constraint re-use. Also, if only one particle is Dynamic, we want it in first position. This isn't a strtct 
					// requirement but some downstream systems assume this is true (e.g., CCD, TriMesh collision).
					TGeometryParticleHandle<FReal, 3>* ParticleA = Particle1.Handle();
					TGeometryParticleHandle<FReal, 3>* ParticleB = Particle2.Handle();
					const bool bSwapOrder = ShouldSwapParticleOrder(ParticleA, ParticleB);
					if (bSwapOrder)
					{
						Swap(ParticleA, ParticleB);
					}

					FParticlePairMidPhase* MidPhase = NarrowPhase.GetParticlePairMidPhase(ParticleA, ParticleB, Particle1.Handle());
					if (MidPhase)
					{
						MidPhasePairs.Add(MidPhase);
					}
				}

				const int32  PrefetchLookahead = 4;

				for (int32 Index = 0; Index < MidPhasePairs.Num() && Index < PrefetchLookahead; Index++)
				{
					MidPhasePairs[Index]->CachePrefetch();
				}


				for (int32 Index = 0; Index < MidPhasePairs.Num(); Index++)
				{
					// Prefetch next pair
					if (Index + PrefetchLookahead  < MidPhasePairs.Num())
					{
						MidPhasePairs[Index + PrefetchLookahead]->CachePrefetch();
					}

					FParticlePairMidPhase* MidPhasePair = MidPhasePairs[Index];
					NarrowPhase.GenerateCollisions(Dt, MidPhasePair, false);
				}

				PHYSICS_CSV_CUSTOM_EXPENSIVE(PhysicsCounters, NumFromBroadphase, NumPotentials, ECsvCustomStatOp::Accumulate);
			}
		}

		const FPBDRigidsSOAs& Particles;
		const FAccelerationStructure* SpatialAcceleration;

		FIgnoreCollisionManager IgnoreCollisionManager;
	};
}
