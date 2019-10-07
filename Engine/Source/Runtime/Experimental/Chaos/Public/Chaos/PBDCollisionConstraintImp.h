// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PBDCollisionConstraint.h"
#include "Chaos/ChaosPerfTest.h"

namespace Chaos
{
#if !UE_BUILD_SHIPPING

	static constexpr int32 BucketSizes2[] = { 0,1,4,8,16,32,64,128,512,MAX_int32 };


	template <bool bGatherStats>
	struct FStatHelper2
	{
		int32 MaxCount;

		FStatHelper2() {}
		void Record(int32 Count) {}
		FString ToString() const { return TEXT(""); }
	};

	template <>
	struct FStatHelper2<true>
	{
		int32 BucketCount[UE_ARRAY_COUNT(BucketSizes2)];
		int32 MaxCount;

		FStatHelper2()
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

#endif

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

		bool VisitSweep(TSpatialVisitorData<TAccelerationStructureHandle<T, d>>, T Length)
		{
			check(false);
			return false;
		}

		bool VisitRaycast(TSpatialVisitorData<TAccelerationStructureHandle<T, d>>, T Length)
		{
			check(false);
			return false;
		}
	};

	extern CHAOS_API int32 EnableCollisions;
	extern CHAOS_API int32 CollisionConstraintsForceSingleThreaded;

	DECLARE_CYCLE_STAT_EXTERN(TEXT("ComputeConstraints"), STAT_ComputeConstraints, STATGROUP_Chaos, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ComputeConstraintsNP"), STAT_ComputeConstraintsNP, STATGROUP_Chaos, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ComputeConstraintsBP"), STAT_ComputeConstraintsBP, STATGROUP_Chaos, CHAOS_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ComputeConstraintsSU"), STAT_ComputeConstraintsSU, STATGROUP_Chaos, CHAOS_API);

	template <typename T, int d>
	template <bool bGatherStats, typename SPATIAL_ACCELERATION>
	void TPBDCollisionConstraint<T, d>::ComputeConstraintsHelperLowLevel(const SPATIAL_ACCELERATION& InSpatialAcceleration, T Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeConstraints);
		CHAOS_SCOPED_TIMER(ComputeConstraints);
		if (!EnableCollisions) return;
		// Broad phase

#if !UE_BUILD_SHIPPING
		FStatHelper2<bGatherStats> BroadphasePotentials;
		FStatHelper2<bGatherStats> NarrowPhaseSkipped;
		FStatHelper2<bGatherStats> NarrowPhasePerformed;
		FStatHelper2<bGatherStats> NarrowPhaseRejected;
		FStatHelper2<bGatherStats> BoundsDistribution;
		int32 SimulatedParticles = 0;
#endif
		{
			SCOPE_CYCLE_COUNTER(STAT_ComputeConstraintsNP);


			// Narrow phase
			CHAOS_SCOPED_TIMER(ComputeConstraints_NP);

			TQueue<TRigidBodyContactConstraint<T, d>, EQueueMode::Mpsc> Queue;	//todo(ocohen): use per thread buffer instead, need better support than ParallelFor for this
			Particles.GetNonDisabledDynamicView().ParallelFor([&](auto& Particle1, int32 ActiveIdxIdx)
			{
#if !UE_BUILD_SHIPPING
				if (bGatherStats)
				{
					++SimulatedParticles;
				}
#endif

				TArray<TAccelerationStructureHandle<T, d>> PotentialIntersections;

				const bool bBody1Bounded = HasBoundingBox(Particle1);

				const T Box1Thickness = ComputeThickness(Particle1, Dt).Size();
				if (bBody1Bounded)
				{
#if CHAOS_PARTICLEHANDLE_TODO
					const TBox <T, d> Box1 = InSpatialAcceleration.GetWorldSpaceBoundingBox(Particle1);
#else
					const TBox <T, d> Box1 = ComputeWorldSpaceBoundingBox(Particle1);	//NOTE: this ignores the velocity expansion which is wrong
#endif

#if !UE_BUILD_SHIPPING
					if (bGatherStats)
					{
						//const TBox<T, d> Box1 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body1Index);

						BoundsDistribution.Record(Box1.Extents().GetMax());
					}
#endif
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

#if !UE_BUILD_SHIPPING
				BroadphasePotentials.Record(PotentialIntersections.Num());

				int32 CountNP = 0;
				int32 RejectedNP = 0;
#endif
				const int32 NumPotentials = PotentialIntersections.Num();
				for (int32 i = 0; i < NumPotentials; ++i)
				{
					auto& Particle2 = *PotentialIntersections[i].GetGeometryParticleHandle_PhysicsThread();
					const TGenericParticleHandle<T, d> Particle2Generic(&Particle2);
					// Collision group culling...
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

					if (bBody1Bounded == bBody2Bounded && Particle2.AsDynamic())
					{
						//if both are dynamic, assume index order matters
#if CHAOS_DETERMINISTIC
						if (Particle2.ParticleID() > Particle1.ParticleID())
#else
					//not deterministic just use memory address to avoid pair duplication
						if (Particle2.Handle() > Particle1.Handle())
#endif
						{
							continue;
						}
					}
					//				++TotalTested;

					const TVector<T, d> Box2Thickness = Particle2.AsDynamic() ? ComputeThickness(*Particle2.AsDynamic(), Dt) : TVector<T, d>(0);
					const T UseThickness = FMath::Max(Box1Thickness, Box2Thickness.Size());// + MThickness

					auto Constraint = ComputeConstraint(Particle1.Handle(), Particle2.Handle(), UseThickness);

					//if (true || !InParticles.Geometry(Body1Index)->HasBoundingBox() || !InParticles.Geometry(Body2Index)->HasBoundingBox())
					{
						//SCOPE_CYCLE_COUNTER(STAT_ComputeConstraintsNP3);
						//use narrow phase to determine if constraint is needed. Without this we can't do shock propagation

#if !UE_BUILD_SHIPPING
						if (bGatherStats)
						{
							++CountNP;
						}
#endif
						UpdateConstraint<ECollisionUpdateType::Any>(UseThickness, Constraint);

						if (Constraint.Phi < UseThickness)
						{
							Queue.Enqueue(Constraint);
						}
						else
						{
#if !UE_BUILD_SHIPPING
							++RejectedNP;
#endif
						}
					}

				}

#if !UE_BUILD_SHIPPING
				NarrowPhasePerformed.Record(CountNP);
				const int32 NPSkipped = NumPotentials - CountNP;
				NarrowPhaseSkipped.Record(NPSkipped);
				NarrowPhaseRejected.Record(RejectedNP);
#endif
			}, bGatherStats || CollisionConstraintsForceSingleThreaded);
			
			{
				SCOPE_CYCLE_COUNTER(STAT_ComputeConstraintsSU);

				while (!Queue.IsEmpty())
				{
					const TRigidBodyContactConstraint<T, d> * Constraint = Queue.Peek();
					FConstraintHandleID HandleID = GetConstraintHandleID(*Constraint);
					if (Handles.Contains(HandleID))
					{
						FConstraintHandle* Handle = Handles[HandleID];
						int32 Idx = Handle->GetConstraintIndex();
						FVector Position = Constraints[Idx].Location;

						Queue.Dequeue(Constraints[Idx]);
						Constraints[Idx].PreviousLocation = Position;
						Constraints[Idx].Lifespan = LifespanCounter;
					}
					else
					{
						int32 Idx = Constraints.AddUninitialized(1);
						Queue.Dequeue(Constraints[Idx]);
						Handles.Add(GetConstraintHandleID(Idx), HandleAllocator.AllocHandle(this, Idx));
						Constraints[Idx].Lifespan = LifespanCounter;
					}
				}
				LifespanCounter++;
			}
		}

#if CHAOS_DETERMINISTIC
		//todo: sort constraints
#endif

#if !UE_BUILD_SHIPPING
		FString OutLog;
		if (bGatherStats)
		{
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
#endif
		//	if (TotalTested > 0)
		//	{
				//UE_LOG(LogChaos, Warning, TEXT("ComputeConstraints: rejected:%f out of rejected:%f = %.3f"), TotalRejected, TotalTested, TotalRejected / TotalTested);
		//	}
	}
}