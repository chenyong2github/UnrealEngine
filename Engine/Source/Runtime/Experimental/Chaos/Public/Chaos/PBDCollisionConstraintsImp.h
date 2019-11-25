// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PBDCollisionConstraints.h"
#include "Chaos/ChaosPerfTest.h"

namespace Chaos
{
	namespace CollisionStats
	{

		static constexpr int32 BucketSizes2[] = { 0,1,4,8,16,32,64,128,512,MAX_int32 };

		template <bool bGatherStats>
		struct FStatHelper
		{
			int32 MaxCount;

			FStatHelper() {}
			void Record(int32 Count) {}
			FString ToString() const { return TEXT(""); }
		};

		template <>
		struct FStatHelper<true>
		{
			int32 BucketCount[UE_ARRAY_COUNT(BucketSizes2)];
			int32 MaxCount;

			FStatHelper()
			{
				FMemory::Memset(BucketCount, 0, sizeof(BucketCount));
				MaxCount = 0;
			}

			void Record(int32 Count)
			{
				for (int32 BucketIdx = 1; BucketIdx < UE_ARRAY_COUNT(BucketSizes2); ++BucketIdx)
				{
					if (Count >= BucketSizes2[BucketIdx - 1] && Count < BucketSizes2[BucketIdx])
					{
						++BucketCount[BucketIdx];
					}
				}

				if (Count > MaxCount)
				{
					MaxCount = Count;
				}
			}

			FString ToString() const
			{
				FString OutLog;
				int32 MaxBucketCount = 0;
				for (int32 Count : BucketCount)
				{
					if (Count > MaxBucketCount)
					{
						MaxBucketCount = Count;
					}
				}

				const float CountPerChar = MaxBucketCount / 20.f;
				for (int32 Idx = 1; Idx < UE_ARRAY_COUNT(BucketSizes2); ++Idx)
				{
					int32 NumChars = BucketCount[Idx] / CountPerChar;
					if (Idx < UE_ARRAY_COUNT(BucketSizes2) - 1)
					{
						OutLog += FString::Printf(TEXT("\t[%4d - %4d) (%4d) |"), BucketSizes2[Idx - 1], BucketSizes2[Idx], BucketCount[Idx]);
					}
					else
					{
						OutLog += FString::Printf(TEXT("\t[%4d -  inf) (%4d) |"), BucketSizes2[Idx - 1], BucketCount[Idx]);
					}
					for (int32 Count = 0; Count < NumChars; ++Count)
					{
						OutLog += TEXT("-");
					}
					OutLog += TEXT("\n");
				}

				return OutLog;
			}
		};

		template <bool bGatherStats, class T = float, int32 d = 3>
		struct FStatData
		{
			FStatData() {}

			void IncrementSimulatedParticles() {}
			void RecordBoundsData(const TBox <T, d>& Box1) {}
			void RecordBroadphasePotentials(int32 Num) {}
			void IncrementCountNP(int32 Count = 1) {}
			void IncrementRejectedNP() {}
			void FinalizeData() {}
			void Print() {}
		};


		template <>
		struct FStatData<true, float, 3>
		{
			FStatData() : CountNP(0), RejectedNP(0), SimulatedParticles(0), NumPotentials(0) {}


			void IncrementSimulatedParticles()
			{
				++SimulatedParticles;
			}

			void RecordBoundsData(const TBox <float, 3>& Box1)
			{
				BoundsDistribution.Record(Box1.Extents().GetMax());
			}

			void RecordBroadphasePotentials(int32 Num)
			{
				NumPotentials = Num;
				BroadphasePotentials.Record(Num);
			}

			void IncrementCountNP(int32 Count = 1)
			{
				CountNP += Count;
			}

			void IncrementRejectedNP()
			{
				++RejectedNP;
			}

			void FinalizeData()
			{
				NarrowPhasePerformed.Record(CountNP);
				const int32 NPSkipped = NumPotentials - CountNP;
				NarrowPhaseSkipped.Record(NPSkipped);
				NarrowPhaseRejected.Record(RejectedNP);
			}

			void Print()
			{
				FString OutLog;
#if CHAOS_PARTICLEHANDLE_TODO
				const float NumParticles = InParticles.Size();
				OutLog = FString::Printf(TEXT("ComputeConstraints stats:\n"
					"Total Particles:%d\nSimulated Particles:%d (%.2f%%)\n"
					"Max candidates per instance:%d (%.2f%%)\n"
					"Max candidates skipped per instance (NP skipped):%d (%.2f%%)\n"
					"Max narrow phase tests per instance:%d (%.2f%%)\n"
					"Max narrow phase rejected per instance (NP rejected):%d (%.2f%%)\n"
					"Constraints generated:%d\n"
				),
					InParticles.Size(),
					SimulatedParticles, SimulatedParticles / NumParticles * 100.f,
					BroadphasePotentials.MaxCount, BroadphasePotentials.MaxCount / NumParticles * 100.f,
					NarrowPhaseSkipped.MaxCount, NarrowPhaseSkipped.MaxCount / NumParticles * 100.f,
					NarrowPhasePerformed.MaxCount, NarrowPhasePerformed.MaxCount / NumParticles * 100.f,
					NarrowPhaseRejected.MaxCount, NarrowPhaseRejected.MaxCount / NumParticles * 100.f,
					Constraints.Num()
				);

				OutLog += FString::Printf(TEXT("Potentials per instance distribution:\n"));
				OutLog += BroadphasePotentials.ToString();


				OutLog += FString::Printf(TEXT("\nCandidates skipped per instance (NP skipped) distribution:\n"));
				OutLog += NarrowPhaseSkipped.ToString();

				OutLog += FString::Printf(TEXT("\nNarrow phase performed per instance distribution:\n"));
				OutLog += NarrowPhasePerformed.ToString();

				OutLog += FString::Printf(TEXT("\nNarrow phase candidates rejected per instance distribution:\n"));
				OutLog += NarrowPhaseRejected.ToString();

				OutLog += FString::Printf(TEXT("\nBounds distribution:\n"));
				OutLog += BoundsDistribution.ToString();

				UE_LOG(LogChaos, Warning, TEXT("%s"), *OutLog);
				PendingHierarchyDump = 0;
#endif
			}

			int32 CountNP;
			int32 RejectedNP;
			int32 SimulatedParticles;
			int32 NumPotentials;
			FStatHelper<true> BroadphasePotentials;
			FStatHelper<true> NarrowPhaseSkipped;
			FStatHelper<true> NarrowPhasePerformed;
			FStatHelper<true> NarrowPhaseRejected;
			FStatHelper<true> BoundsDistribution;
		};
	}


	extern CHAOS_API int32 EnableCollisions;
	extern CHAOS_API int32 CollisionConstraintsForceSingleThreaded;

#if UE_BUILD_SHIPPING
#define COLLISION_OPT_OUT(X)
	const bool bDisableCollisionParallelFor = false;
#else
#define COLLISION_OPT_OUT(X) X;
	CHAOS_API extern bool bDisableCollisionParallelFor;
#endif





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
	*
	*/

	DECLARE_CYCLE_STAT_EXTERN(TEXT("ComputeConstraints"), STAT_ComputeConstraints, STATGROUP_Chaos, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ComputeConstraintsSU"), STAT_ComputeConstraintsSU, STATGROUP_Chaos, CHAOS_API);

	template <typename T, int d>
	template <bool bGatherStats, typename SPATIAL_ACCELERATION>
	void TPBDCollisionConstraints<T, d>::ComputeConstraintsHelperLowLevel(const SPATIAL_ACCELERATION& InSpatialAcceleration, T Dt)
	{
		COLLISION_OPT_OUT(CollisionStats::FStatData<bGatherStats> StatData);
		SCOPE_CYCLE_COUNTER(STAT_ComputeConstraints);
		CHAOS_SCOPED_TIMER(ComputeConstraints);
		if (!EnableCollisions) return;

		//todo(ocohen): use per thread buffer instead, need better support than ParallelFor for this
		TQueue<FCollisionConstraintBase*, EQueueMode::Mpsc> Queue;

		Particles.GetNonDisabledDynamicView().ParallelFor([&](auto& Particle1, int32 ActiveIdxIdx)
		{
			COLLISION_OPT_OUT(StatData.IncrementSimulatedParticles());

			TArray<TAccelerationStructureHandle<T, d>> PotentialIntersections;

			const bool bBody1Bounded = HasBoundingBox(Particle1);

			const T Box1Thickness = ComputeThickness(Particle1, CollisionVelocityInflation * Dt).Size();
			if (bBody1Bounded)
			{
#if CHAOS_PARTICLEHANDLE_TODO
				const TBox <T, d> Box1 = InSpatialAcceleration.GetWorldSpaceBoundingBox(Particle1);
#else
				const TBox <T, d> Box1 = ComputeWorldSpaceBoundingBox(Particle1);	//NOTE: this ignores the velocity expansion which is wrong
#endif

				COLLISION_OPT_OUT(StatData.RecordBoundsData(Box1));

				TSimOverlapVisitor<T, d> OverlapVisitor(PotentialIntersections);
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

			COLLISION_OPT_OUT(StatData.RecordBroadphasePotentials(PotentialIntersections.Num()))

				const int32 NumPotentials = PotentialIntersections.Num();
			for (int32 i = 0; i < NumPotentials; ++i)
			{
				auto& Particle2 = *PotentialIntersections[i].GetGeometryParticleHandle_PhysicsThread();
				const TGenericParticleHandle<T, d> Particle2Generic(&Particle2);

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

				const TVector<T, d> Box2Thickness = bIsParticle2Dynamic ? ComputeThickness(*Particle2.CastToRigidParticle(), Dt) : TVector<T, d>(0);
				const T UseThickness = FMath::Max(Box1Thickness, Box2Thickness.Size());// + MThickness

				FCollisionConstraintsArray Constraints;
				ConstructConstraints(Particle1.Handle(), Particle2.Handle(), UseThickness, Constraints);
				for (TCollisionConstraintBase<T, d>* ConstraintBase : Constraints)
				{
					Queue.Enqueue(ConstraintBase);
				}

				COLLISION_OPT_OUT(if (Constraints.Num()) { StatData.IncrementCountNP(Constraints.Num()); });
				COLLISION_OPT_OUT(if (!Constraints.Num()) { StatData.IncrementRejectedNP(); });
			}

			COLLISION_OPT_OUT(StatData.FinalizeData());

		}, bGatherStats || bDisableCollisionParallelFor);

		{
			SCOPE_CYCLE_COUNTER(STAT_ComputeConstraintsSU);

			
			FCollisionConstraintBase* ConstraintBase = nullptr;
			while ( Queue.Dequeue(ConstraintBase) )
			{
				if (ConstraintBase->GetType() == TRigidBodyPointContactConstraint<T, 3>::StaticType())
				{
					TRigidBodyPointContactConstraint<T, d>* PointConstraint = ConstraintBase->As< TRigidBodyPointContactConstraint<T, d> >();

					int32 Idx = PointConstraints.AddUninitialized(1);
					PointConstraints[Idx] = *PointConstraint;
					Handles.Add(HandleAllocator.template AllocHandle< TRigidBodyPointContactConstraint<T, d> >(this, Idx));

					delete PointConstraint;
				}
				else if (ConstraintBase->GetType() == TRigidBodyPlaneContactConstraint<T, 3>::StaticType())
				{
					TRigidBodyPlaneContactConstraint<T, d>* PlaneConstraint = ConstraintBase->As< TRigidBodyPlaneContactConstraint<T, d> >();

					int32 Idx = PlaneConstraints.AddUninitialized(1);
					PlaneConstraints[Idx] = *PlaneConstraint;
					Handles.Add(HandleAllocator.template AllocHandle< TRigidBodyPlaneContactConstraint<T, d> >(this, Idx));

					delete PlaneConstraint;
				}
			}
			LifespanCounter++;
		}

#if CHAOS_DETERMINISTIC
		//todo: sort constraints
#endif

		COLLISION_OPT_OUT(StatData.Print());
	}
}