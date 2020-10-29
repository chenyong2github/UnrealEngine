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
		TArray<TAccelerationStructureHandle<FReal, 3>>& Intersections;
		FSimOverlapVisitor(TArray<TAccelerationStructureHandle<FReal, 3>>& InIntersections)
			: Intersections(InIntersections)
		{
		}

		bool VisitOverlap(const TSpatialVisitorData<TAccelerationStructureHandle<FReal, 3>>& Instance)
		{
			Intersections.Add(Instance.Payload);
			return true;
		}

		bool VisitSweep(TSpatialVisitorData<TAccelerationStructureHandle<FReal, 3>>, FQueryFastData& CurData)
		{
			check(false);
			return false;
		}

		bool VisitRaycast(TSpatialVisitorData<TAccelerationStructureHandle<FReal, 3>>, FQueryFastData& CurData)
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
		using FAccelerationStructure = ISpatialAcceleration<TAccelerationStructureHandle<FReal, 3>, FReal, 3>;

		FSpatialAccelerationBroadPhase(const TPBDRigidsSOAs<FReal, 3>& InParticles, const FReal InBoundsExpansion, const FReal InVelocityInflation, const FReal InCullDistance)
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

			if (const auto AABBTree = SpatialAcceleration->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TAABBTreeLeafArray<TAccelerationStructureHandle<FReal, 3>, FReal>, FReal>>())
			{
				ProduceOverlaps(Dt, *AABBTree, NarrowPhase, Receiver, StatData, ResimCache);
			}
			else if (const auto BV = SpatialAcceleration->template As<TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>())
			{
				ProduceOverlaps(Dt, *BV, NarrowPhase, Receiver, StatData, ResimCache);
			}
			else if (const auto AABBTreeBV = SpatialAcceleration->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>, FReal>>())
			{
				ProduceOverlaps(Dt, *AABBTreeBV, NarrowPhase, Receiver, StatData, ResimCache);
			}
			else if (const auto Collection = SpatialAcceleration->template As<ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>())
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
					TGenericParticleHandleHandleImp<FReal,3> GenericHandle(Particle1.Handle());
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

			TArray<TAccelerationStructureHandle<FReal, 3>> PotentialIntersections;

			if (bIsResimming || (Particle1.ObjectState() == EObjectStateType::Dynamic || Particle1.ObjectState() == EObjectStateType::Sleeping))
			{
				const bool bBody1Bounded = HasBoundingBox(Particle1);
				{
					SCOPE_CYCLE_COUNTER(STAT_Collisions_SpatialBroadPhase);
					if (bBody1Bounded)
					{
						const FReal Box1Thickness = ComputeBoundsThickness(Particle1, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
						const TAABB<FReal, 3> Box1 = ComputeWorldSpaceBoundingBox<FReal>(Particle1).ThickenSymmetrically(FVec3(Box1Thickness));

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
					const TGenericParticleHandle<FReal, 3> Particle2Generic(&Particle2);

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
					// Sleeping won't collide against another sleeping and sleeping vs dynamic gets picked up by the other direction.
					const bool bIsParticle2Kinematic = Particle2.CastToKinematicParticle() &&
						(Particle2.ObjectState() == EObjectStateType::Kinematic &&
							(Particle2.CastToKinematicParticle()->V().SizeSquared() > 1e-4 ||
								Particle2.Geometry()->GetType() == TCapsule<float>::StaticType()));
					if (Particle1.ObjectState() == EObjectStateType::Sleeping && !bIsParticle2Kinematic && bSecondParticleWillHaveAnswer)
					{
						//question: if !bSecondParticleWillHaveAnswer do we need to reorder constraint?
						continue;
					}

					// Make sure we don't add a second set of constaint for the same body pair (with the body order flipped)
					const bool bBody2Bounded = HasBoundingBox(Particle2);
					const bool bIsParticle2Dynamic = Particle2.CastToRigidParticle() && Particle2.ObjectState() == EObjectStateType::Dynamic;
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
						// Generate constraints for the potentially overlapping shape pairs. Also run collision detection to generate
						// the contact position and normal (for contacts within CullDistance) for use in collision callbacks.
						NarrowPhase.GenerateCollisions(NewConstraints, Dt, Particle1.Handle(), Particle2.Handle(), CullDistance);
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

		const TPBDRigidsSOAs<FReal, 3>& Particles;
		const FAccelerationStructure* SpatialAcceleration;
		FReal CullDistance;

		FIgnoreCollisionManager IgnoreCollisionManager;
	};




	extern void MoveToTOIPairHack(FReal Dt, TPBDRigidParticleHandle<FReal, 3>* Particle1, const TGeometryParticleHandle<FReal, 3>* Particle2);

	template<typename T_SPATIALACCELERATION>
	void MoveToTOIHackImpl(FReal Dt, TTransientPBDRigidParticleHandle<FReal, 3>& Particle1, const T_SPATIALACCELERATION* SpatialAcceleration)
	{
		TArray<TAccelerationStructureHandle<FReal, 3>> PotentialIntersections;
		const FAABB3 Box1 = ComputeWorldSpaceBoundingBox(Particle1);
		FSimOverlapVisitor OverlapVisitor(PotentialIntersections);
		SpatialAcceleration->Overlap(Box1, OverlapVisitor);

		const int32 NumPotentials = PotentialIntersections.Num();
		for (int32 i = 0; i < NumPotentials; ++i)
		{
			auto& Particle2 = *PotentialIntersections[i].GetGeometryParticleHandle_PhysicsThread();

			// We only care about static/kinematics
			if (Particle2.CastToRigidParticle() && (Particle2.CastToRigidParticle()->ObjectState() == EObjectStateType::Dynamic))
			{
				continue;
			}
			if (!Particle1.Geometry() && !Particle2.Geometry())
			{
				continue;
			}

			const bool bBody2Bounded = HasBoundingBox(Particle2);
			if (Particle1.Handle() == Particle2.Handle())
			{
				continue;
			}

			MoveToTOIPairHack(Dt, Particle1.Handle(), &Particle2);
		}
	}

}
