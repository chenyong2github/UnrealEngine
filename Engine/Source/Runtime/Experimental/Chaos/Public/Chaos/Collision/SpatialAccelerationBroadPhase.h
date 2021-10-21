// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/BroadPhase.h"
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

namespace Chaos
{
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::BroadPhase"), STAT_Collisions_SpatialBroadPhase, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::AABBTree"), STAT_Collisions_AABBTree, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::Filtering"), STAT_Collisions_Filtering, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::Restore"), STAT_Collisions_Restore, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::ComputeBoundsThickness"), STAT_Collisions_ComputeBoundsThickness, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::GenerateCollisions"), STAT_Collisions_GenerateCollisions, STATGROUP_ChaosCollision, CHAOS_API);
	
	template <typename TPayloadType, typename T, int d>
	class ISpatialAcceleration;

	class IResimCacheBase;


	/**
	 *
	 */
	struct FSimOverlapVisitor
	{
		TArray<FAccelerationStructureHandle>& Intersections;
		FSimOverlapVisitor(FUniqueIdx InParticleUniqueIdx, const FCollisionFilterData& InSimFilterData, TArray<FAccelerationStructureHandle>& InIntersections)
			: Intersections(InIntersections)
			, SimFilterData(InSimFilterData)
			, ParticleUniqueIdx(InParticleUniqueIdx)
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

	private:
		FCollisionFilterData SimFilterData;
		FUniqueIdx ParticleUniqueIdx; // unique id of the particle visiting, used to skip self intersection as early as possible
	};

	/**
	 * A broad phase that iterates over particle and uses a spatial acceleration structure to output
	 * potentially overlapping SpatialAccelerationHandles.
	 */
	class FSpatialAccelerationBroadPhase : public FBroadPhase
	{
	public:
		using FAccelerationStructure = ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>;

		FSpatialAccelerationBroadPhase(const FPBDRigidsSOAs& InParticles, const FReal InBoundsExpansion, const FReal InVelocityInflation, const FReal InCullDistance)
			: FBroadPhase(InBoundsExpansion, InVelocityInflation)
			, Particles(InParticles)
			, SpatialAcceleration(nullptr)
			, CullDistance(InCullDistance)
		{
		}

		void SetSpatialAcceleration(const FAccelerationStructure* InSpatialAcceleration)
		{
			SpatialAcceleration = InSpatialAcceleration;
		}

		void SetCullDistance(FReal InCullDistance)
		{
			CullDistance = InCullDistance;
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
				int32 EntryCount = 0;
				const TParticleView<TPBDRigidParticles<FReal, 3>>& View = Particles.GetNonDisabledDynamicView();

				// Calculate maximum possible particle slots
				for(int32 Index = 0; Index < View.SOAViews.Num(); ++Index)
				{
					EntryCount += View.SOAViews[Index].Size();
				}

				View.ParallelFor([&](auto& Particle1,int32 ActiveIdxIdx)
				{
					ProduceParticleOverlaps</*bResimming=*/false>(Dt,Particle1,InSpatialAcceleration,NarrowPhase,ActiveIdxIdx);
				},bDisableParallelFor);
			}
			else
			{
				int32 EntryCount = 0;
				const TParticleView<TGeometryParticles<FReal, 3>>& View = ResimCache->GetDesyncedView();

				// Calculate maximum possible particle slots
				for(int32 Index = 0; Index < View.SOAViews.Num(); ++Index)
				{
					EntryCount += View.SOAViews[Index].Size();
				}

				View.ParallelFor(
					[&](auto& Particle1,int32 ActiveIdxIdx)
				{
					//TODO: use transient handle
					FGenericParticleHandleHandleImp GenericHandle(Particle1.Handle());
					ProduceParticleOverlaps</*bResimming=*/true>(Dt,GenericHandle,InSpatialAcceleration,NarrowPhase,ActiveIdxIdx);
				},bDisableParallelFor);
			}
		}

		FIgnoreCollisionManager& GetIgnoreCollisionManager() { return IgnoreCollisionManager; }

	private:
		template<bool bIsResimming, typename THandle, typename T_SPATIALACCELERATION>
		void ProduceParticleOverlaps(
		    FReal Dt,
		    THandle& Particle1,
		    const T_SPATIALACCELERATION& InSpatialAcceleration,
		    FNarrowPhase& NarrowPhase,
			int32 EntryIndex)
		{
			//Durning non-resim we must pass rigid particles
			static_assert(bIsResimming || THandle::StaticType() == EParticleType::Rigid, "During non-resim we expect rigid particles");

			TArray<FAccelerationStructureHandle> PotentialIntersections;

			if (bIsResimming || (Particle1.ObjectState() == EObjectStateType::Dynamic || Particle1.ObjectState() == EObjectStateType::Sleeping))
			{
				const bool bBody1Bounded = HasBoundingBox(Particle1);
				{
					SCOPE_CYCLE_COUNTER(STAT_Collisions_AABBTree);
					if (bBody1Bounded)
					{
						FCollisionFilterData ParticleSimData;
						FAccelerationStructureHandle::ComputeParticleSimFilterDataFromShapes(Particle1, ParticleSimData);

						const FReal Box1Thickness = ComputeBoundsThickness(Particle1, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
						const FAABB3 Box1 = ComputeWorldSpaceBoundingBox<FReal>(Particle1).ThickenSymmetrically(FVec3(Box1Thickness));

						FSimOverlapVisitor OverlapVisitor(Particle1.UniqueIdx(), ParticleSimData, PotentialIntersections);
						InSpatialAcceleration.Overlap(Box1, OverlapVisitor);
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
				for (int32 i = 0; i < NumPotentials; ++i)
				{
					auto& Particle2 = *PotentialIntersections[i].GetGeometryParticleHandle_PhysicsThread();
					const FGenericParticleHandle Particle2Generic(&Particle2);

					// Broad Phase Culling
					// CollisionGroup == 0 : Collide_With_Everything
					// CollisionGroup == INDEX_NONE : Disabled collisions
					// CollisionGroup_A != CollisionGroup_B : Skip Check

					if(bIsResimming)
					{
						//during resim we allow particle 1 to be kinematic, in that case make sure we don't create any kinematic-kinematic constraints
						if(Particle1.CastToRigidParticle() == nullptr && Particle2.CastToRigidParticle() == nullptr)
						{
							continue;
						}
					}

					if (Particle1.HasCollisionConstraintFlag(ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions) )
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
					
					if ((Particle1.ObjectState() == EObjectStateType::Sleeping) && (Particle2.ObjectState() == EObjectStateType::Sleeping))
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

					// Note:
					// 
					// Normally (not resimming) we iterate over dynamic particles, so:
					// - Particle1 is always dynamic, but may be asleep
					// - Particle2 may be static, kinematic or dynamic, but may be asleep
					// 
					// When resimming we iterate over "desynced" particles which may be kinematic so:
					// - Particle1 is always desynced
					// - Particle2 may also be desynced, in which case we will also visit the opposite ordering regardless of dynamic/kinematic status
					// - Particle1 may be static, kinematic or dynamic, but may be asleep
					// - Particle2 may be static, kinematic or dynamic, but may be asleep
					// 
					// Even though Particle1 may be kinematic when resimming, we want to create the contacts in the original order (i.e., dynamic first)
					//
					const bool bIsParticle2Dynamic = (Particle2.ObjectState() == EObjectStateType::Dynamic) || (Particle2.ObjectState() == EObjectStateType::Sleeping);

					// Used to determine a winner in cases where we will visit particle pairs in both orders
					const bool bIsParticle1Preferred = (Particle2.ParticleID() < Particle1.ParticleID());

					bool bAcceptParticlePair = false;
					if (!bIsResimming)
					{
						// When not resimming, accept pair if any of the following are true
						// (1) Both particles are dynamic|asleep and Particle1 is "preferred" (to prevent duplicate entries when we visit the pair in the opposite order)
						// (2) Particle1 is dynamic|asleep and Particle2 is kinematic (we won't visit the opposite order)
						// Then both conditions can be combined and reduced to:
						bAcceptParticlePair = (bIsParticle1Preferred || !bIsParticle2Dynamic);
					}
					else
					{
						// When resimming, accept pair if any of the following are true
						// (1) Both particles are dynamic|asleep and desynced and Particle1 is preferred (we will visit both pair orderings)
						// (2) Both particles are dynamic|asleep but Particle2 is not desynced (we won't visit the opposite order)
						// (3) Particle1 is dynamic|sleeping and Particle2 is kinematic (regardless of desync state, see (4))
						// (4) Particle1 is kinematic and Particle2 is dynamic but not desynced (we won't visit the opposite order)
						// We should skip non-dynamic pairs should they show up (implicit in the logic below)
						const bool bIsParticle1Dynamic = (Particle1.ObjectState() == EObjectStateType::Dynamic) || (Particle1.ObjectState() == EObjectStateType::Sleeping);
						const bool bIsParticle2Desynced = (Particle2.SyncState() == ESyncState::HardDesync);
						bAcceptParticlePair = 
							(bIsParticle1Dynamic && bIsParticle2Dynamic && (bIsParticle1Preferred || !bIsParticle2Desynced))		// Case (1) and (2)
							|| (bIsParticle1Dynamic && !bIsParticle2Dynamic)														// Case (3)
							|| (!bIsParticle1Dynamic && bIsParticle2Dynamic && !bIsParticle2Desynced);								// Case (4)

						// @todo(chaos): for Case (4) above, we should be switching the order of the particles before continuing
						// if we want this to be properly deterministic - we always put dynamics first in the non-resim path...
					}

					if (!bAcceptParticlePair)
					{
						continue;
					}

					// Try to restore all the contacts for this pair. This will only succeed if they have not moved (within some threshold)
					{
						SCOPE_CYCLE_COUNTER(STAT_Collisions_Restore);
						if (NarrowPhase.TryRestoreCollisions(Dt, Particle1.Handle(), Particle2.Handle()))
						{
							continue;
						}
					}
					
					// If we get here, we need to run the narrow phase to possibly generate new contacts, or refresh existing ones
					{
						SCOPE_CYCLE_COUNTER(STAT_Collisions_GenerateCollisions);

						// We move the bodies during contact resolution and it may be in any direction
						// @todo(chaos): this expansion can be very large for some objects - we may want to consider extending only along
						// the velocity direction if CCD is not enabled for either object.
						const FReal CullDistance1 = ComputeBoundsThickness(Particle1, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
						FReal CullDistance2 = 0.0f;
						if (FKinematicGeometryParticleHandle* KinematicParticle2 = Particle2.CastToKinematicParticle())
						{
							CullDistance2 = ComputeBoundsThickness(*KinematicParticle2, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
						}
						const FReal NetCullDistance = CullDistance + CullDistance1 + CullDistance2;

						// Generate constraints for the potentially overlapping shape pairs. Also run collision detection to generate
						// the contact position and normal (for contacts within CullDistance) for use in collision callbacks.
						NarrowPhase.GenerateCollisions(Dt, Particle1.Handle(), Particle2.Handle(), NetCullDistance, false);
					}
				}
			}
		}

		const FPBDRigidsSOAs& Particles;
		const FAccelerationStructure* SpatialAcceleration;
		FReal CullDistance;

		FIgnoreCollisionManager IgnoreCollisionManager;
	};
}
