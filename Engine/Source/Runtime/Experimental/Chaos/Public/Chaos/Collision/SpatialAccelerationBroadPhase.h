// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/BroadPhase.h"
#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/StatsData.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Capsule.h"
#include "ChaosStats.h"
#include "Chaos/EvolutionResimCache.h"
#include "Chaos/GeometryParticlesfwd.h"

namespace Chaos
{
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::BroadPhase"), STAT_Collisions_SpatialBroadPhase, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::Filtering"), STAT_Collisions_Filtering, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::ComputeBoundsThickness"), STAT_Collisions_ComputeBoundsThickness, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::GenerateCollisions"), STAT_Collisions_GenerateCollisions, STATGROUP_ChaosCollision, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::ReceiveCollisions"), STAT_Collisions_ReceiveCollisions, STATGROUP_ChaosCollision, CHAOS_API);
	
	template <typename TPayloadType, typename T, int d>
	class ISpatialAcceleration;

	class IResimCacheBase;


	/**
	 *
	 */
	struct FSimOverlapVisitor
	{
		TArray<FAccelerationStructureHandle>& Intersections;
		FSimOverlapVisitor(TArray<FAccelerationStructureHandle>& InIntersections)
			: Intersections(InIntersections)
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
			FAsyncCollisionReceiver& Receiver,
			CollisionStats::FStatData& StatData,
			IResimCacheBase* ResimCache
			)
		{
			if (!ensure(SpatialAcceleration))
			{
				// Must call SetSpatialAcceleration
				return;
			}

			if (const auto AABBTree = SpatialAcceleration->template As<TAABBTree<FAccelerationStructureHandle, TAABBTreeLeafArray<FAccelerationStructureHandle>>>())
			{
				ProduceOverlaps(Dt, *AABBTree, NarrowPhase, Receiver, StatData, ResimCache);
			}
			else if (const auto BV = SpatialAcceleration->template As<TBoundingVolume<FAccelerationStructureHandle>>())
			{
				ProduceOverlaps(Dt, *BV, NarrowPhase, Receiver, StatData, ResimCache);
			}
			else if (const auto AABBTreeBV = SpatialAcceleration->template As<TAABBTree<FAccelerationStructureHandle, TBoundingVolume<FAccelerationStructureHandle>>>())
			{
				ProduceOverlaps(Dt, *AABBTreeBV, NarrowPhase, Receiver, StatData, ResimCache);
			}
			else if (const auto Collection = SpatialAcceleration->template As<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>>())
			{
				Collection->PBDComputeConstraintsLowLevel(Dt, *this, NarrowPhase, Receiver, StatData, ResimCache);
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
			FAsyncCollisionReceiver& Receiver,
			CollisionStats::FStatData& StatData,
			IResimCacheBase* ResimCache
			)
		{
			const bool bDisableParallelFor = StatData.IsEnabled() || bDisableCollisionParallelFor;
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

				Receiver.Prepare(EntryCount);

				View.ParallelFor([&](auto& Particle1,int32 ActiveIdxIdx)
				{
					ProduceParticleOverlaps</*bResimming=*/false>(Dt,Particle1,InSpatialAcceleration,NarrowPhase,Receiver,StatData, ActiveIdxIdx);
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

				Receiver.Prepare(EntryCount);

				View.ParallelFor(
					[&](auto& Particle1,int32 ActiveIdxIdx)
				{
					//TODO: use transient handle
					FGenericParticleHandleHandleImp GenericHandle(Particle1.Handle());
					ProduceParticleOverlaps</*bResimming=*/true>(Dt,GenericHandle,InSpatialAcceleration,NarrowPhase,Receiver,StatData, ActiveIdxIdx);
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
		    FAsyncCollisionReceiver& Receiver,
		    CollisionStats::FStatData& StatData,
			int32 EntryIndex)
		{
			CHAOS_COLLISION_STAT(StatData.IncrementSimulatedParticles());

			//Durning non-resim we must pass rigid particles
			static_assert(bIsResimming || THandle::StaticType() == EParticleType::Rigid, "During non-resim we expect rigid particles");

			TArray<FAccelerationStructureHandle> PotentialIntersections;

			if (bIsResimming || (Particle1.ObjectState() == EObjectStateType::Dynamic || Particle1.ObjectState() == EObjectStateType::Sleeping))
			{
				const bool bBody1Bounded = HasBoundingBox(Particle1);
				{
					SCOPE_CYCLE_COUNTER(STAT_Collisions_SpatialBroadPhase);
					if (bBody1Bounded)
					{
						const FReal Box1Thickness = ComputeBoundsThickness(Particle1, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
						const FAABB3 Box1 = ComputeWorldSpaceBoundingBox<FReal>(Particle1).ThickenSymmetrically(FVec3(Box1Thickness));

						CHAOS_COLLISION_STAT(StatData.RecordBoundsData(Box1));

						FSimOverlapVisitor OverlapVisitor(PotentialIntersections);
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

					CHAOS_COLLISION_STAT(StatData.RecordBroadphasePotentials(PotentialIntersections.Num()))
				}

				SCOPE_CYCLE_COUNTER(STAT_Collisions_Filtering);
				const int32 NumPotentials = PotentialIntersections.Num();
				FCollisionConstraintsArray NewConstraints;
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

					if (Particle1.Handle() == Particle2.Handle())
					{
						continue;
					}

					// HACK : This should not be happening if the disabled particles are properly removed from the active particles list. 
					if (Particle1.Disabled() || Particle2Generic->Disabled())
					{
						continue;
					}

					const bool bSecondParticleWillHaveAnswer = !bIsResimming || Particle2.SyncState() == ESyncState::HardDesync;
					// Sleeping vs dynamic gets picked up by the other direction.
					const bool bIsParticle2Dynamic = Particle2.CastToRigidParticle() && Particle2.ObjectState() == EObjectStateType::Dynamic;
					if (Particle1.ObjectState() == EObjectStateType::Sleeping && bIsParticle2Dynamic && bSecondParticleWillHaveAnswer)
					{
						//question: if !bSecondParticleWillHaveAnswer do we need to reorder constraint?
						continue;
					}

					// Make sure we don't add a second set of constaint for the same body pair (with the body order flipped)
					const bool bBody2Bounded = HasBoundingBox(Particle2);
					if (bBody1Bounded == bBody2Bounded && bIsParticle2Dynamic)
					{
						if (Particle1.ParticleID() < Particle2.ParticleID() && bSecondParticleWillHaveAnswer)
						{
							//question: if !bSecondParticleWillHaveAnswer do we need to reorder constraint?
							continue;
						}
					}
				
					
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
						NarrowPhase.GenerateCollisions(NewConstraints, Dt, Particle1.Handle(), Particle2.Handle(), NetCullDistance);
					}
				}

				{
					SCOPE_CYCLE_COUNTER(STAT_Collisions_ReceiveCollisions);

					CHAOS_COLLISION_STAT(if (NewConstraints.Num()) { StatData.IncrementCountNP(NewConstraints.Num()); });
					CHAOS_COLLISION_STAT(if (!NewConstraints.Num()) { StatData.IncrementRejectedNP(); });

					// We are probably running in a parallel task here. The Receiver collects the contacts from all the tasks 
					// and passes them to theconstraint container in serial.
					Receiver.ReceiveCollisions(MoveTemp(NewConstraints), EntryIndex);
				}
			}

			CHAOS_COLLISION_STAT(StatData.FinalizeData());
		}

		const FPBDRigidsSOAs& Particles;
		const FAccelerationStructure* SpatialAcceleration;
		FReal CullDistance;

		FIgnoreCollisionManager IgnoreCollisionManager;
	};
}
