// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/BroadPhase.h"
#include "Chaos/Collision/StatsData.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace Chaos
{
	class FAsyncCollisionReceiver;

	template <typename TPayloadType, typename T, int d>
	class ISpatialAcceleration;

	/**
	 *
	 */
	template <typename T, int d>
	struct TSimOverlapVisitor
	{
		TArray<TAccelerationStructureHandle<T, d>>& Intersections;
		TSimOverlapVisitor(TArray<TAccelerationStructureHandle<T, d>>& InIntersections)
			: Intersections(InIntersections)
		{
		}

		bool VisitOverlap(const TSpatialVisitorData<TAccelerationStructureHandle<T, d>>& Instance)
		{
			Intersections.Add(Instance.Payload);
			return true;
		}

		bool VisitSweep(TSpatialVisitorData<TAccelerationStructureHandle<T, d>>, FQueryFastData& CurData)
		{
			check(false);
			return false;
		}

		bool VisitRaycast(TSpatialVisitorData<TAccelerationStructureHandle<T, d>>, FQueryFastData& CurData)
		{
			check(false);
			return false;
		}
	};

	/**
	 * A broad phase that iterates over particle and uses a spatial acceleration structure to output
	 * potentially overlapping SpatialAccelerationHandles.
	 */
	class FSpatialAccelerationBroadPhase : public FBroadPhase
	{
	public:
		using FAccelerationStructure = ISpatialAcceleration<TAccelerationStructureHandle<FReal, 3>, FReal, 3>;

		FSpatialAccelerationBroadPhase(const TPBDRigidsSOAs<FReal, 3>& InParticles, const FReal InThickness, const FReal InVelocityInflation)
			: FBroadPhase(InThickness, InVelocityInflation)
			, Particles(InParticles)
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
			FAsyncCollisionReceiver& Receiver,
			CollisionStats::FStatData& StatData)
		{
			if (!ensure(SpatialAcceleration))
			{
				// Must call SetSpatialAcceleration
				return;
			}

			if (const auto AABBTree = SpatialAcceleration->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TAABBTreeLeafArray<TAccelerationStructureHandle<FReal, 3>, FReal>, FReal>>())
			{
				ProduceOverlaps(Dt, *AABBTree, NarrowPhase, Receiver, StatData);
			}
			else if (const auto BV = SpatialAcceleration->template As<TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>())
			{
				ProduceOverlaps(Dt, *BV, NarrowPhase, Receiver, StatData);
			}
			else if (const auto AABBTreeBV = SpatialAcceleration->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>, FReal>>())
			{
				ProduceOverlaps(Dt, *AABBTreeBV, NarrowPhase, Receiver, StatData);
			}
			else if (const auto Collection = SpatialAcceleration->template As<ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>())
			{
				Collection->PBDComputeConstraintsLowLevel(Dt, *this, NarrowPhase, Receiver, StatData);
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
			CollisionStats::FStatData& StatData)
		{
			const bool bDisableParallelFor = StatData.IsEnabled() || bDisableCollisionParallelFor;

			Particles.GetNonDisabledDynamicView().ParallelFor(
				[&](auto& Particle1, int32 ActiveIdxIdx)
				{
					ProduceParticleOverlaps(Dt, Particle1, InSpatialAcceleration, NarrowPhase, Receiver, StatData);
				}, bDisableParallelFor);
		}

	private:
		template<typename T_SPATIALACCELERATION>
		void ProduceParticleOverlaps(
		    FReal Dt,
		    TTransientPBDRigidParticleHandle<FReal, 3>& Particle1,
		    const T_SPATIALACCELERATION& InSpatialAcceleration,
		    FNarrowPhase& NarrowPhase,
		    FAsyncCollisionReceiver& Receiver,
		    CollisionStats::FStatData& StatData)
		{
			CHAOS_COLLISION_STAT(StatData.IncrementSimulatedParticles());

			TArray<TAccelerationStructureHandle<FReal, 3>> PotentialIntersections;

			if (Particle1.CastToRigidParticle() && Particle1.ObjectState() == EObjectStateType::Dynamic)
			{
				// @todo(ccaulfield): the spatial acceleration scheme needs to know the expanded bboxes for all particles, not just the one doing the test
				const bool bBody1Bounded = HasBoundingBox(Particle1);
				const FReal Box1Thickness = ComputeBoundsThickness(Particle1, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
				if (bBody1Bounded)
				{
					// @todo(ccaulfield): COLLISION - see the NOTE below - fix it
#if CHAOS_PARTICLEHANDLE_TODO
					const TAABB<FReal, 3> Box1 = InSpatialAcceleration.GetWorldSpaceBoundingBox(Particle1);
#else
					const TAABB<FReal, 3> Box1 = ComputeWorldSpaceBoundingBox(Particle1); // NOTE: this ignores the velocity expansion which is wrong
#endif

					CHAOS_COLLISION_STAT(StatData.RecordBoundsData(Box1));

					TSimOverlapVisitor<FReal, 3> OverlapVisitor(PotentialIntersections);
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

				const int32 NumPotentials = PotentialIntersections.Num();
				for (int32 i = 0; i < NumPotentials; ++i)
				{
					auto& Particle2 = *PotentialIntersections[i].GetGeometryParticleHandle_PhysicsThread();
					const TGenericParticleHandle<FReal, 3> Particle2Generic(&Particle2);

					// Broad Phase Culling
					// CollisionGroup == 0 : Collide_With_Everything
					// CollisionGroup == INDEX_NONE : Disabled collisions
					// CollisionGroup_A != CollisionGroup_B : Skip Check

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

					const bool bBody2Bounded = HasBoundingBox(Particle2);

					if (Particle1.Handle() == Particle2.Handle())
					{
						continue;
					}

					const bool bIsParticle2Dynamic = Particle2.CastToRigidParticle() && Particle2.ObjectState() == EObjectStateType::Dynamic;
					if (bBody1Bounded == bBody2Bounded && bIsParticle2Dynamic)
					{
						//no bidirectional constraints.
						if (Particle2.ParticleID() > Particle1.ParticleID())
						{
							continue;
						}
					}

					const FReal Box2Thickness = bIsParticle2Dynamic ? ComputeBoundsThickness(*Particle2.CastToRigidParticle(), Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size() : (FReal)0;

					NarrowPhase.GenerateCollisions(Dt, Receiver, Particle1.Handle(), Particle2.Handle(), FMath::Max(Box1Thickness, Box2Thickness), StatData);
				}
			}

			CHAOS_COLLISION_STAT(StatData.FinalizeData());
		}

		const TPBDRigidsSOAs<FReal, 3>& Particles;
		const FAccelerationStructure* SpatialAcceleration;
	};
}