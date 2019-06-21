// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraint.h"

#include "Chaos/BoundingVolume.h"
#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/Defines.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "ChaosStats.h"
#include "ChaosLog.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/ChaosPerfTest.h"
#include "Containers/Queue.h"

#if INTEL_ISPC
#if USING_CODE_ANALYSIS
    MSVC_PRAGMA( warning( push ) )
    MSVC_PRAGMA( warning( disable : ALL_CODE_ANALYSIS_WARNINGS ) )
#endif    // USING_CODE_ANALYSIS

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonportable-include-path"
#endif

#include "PBDCollisionConstraint.ispc.generated.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if USING_CODE_ANALYSIS
    MSVC_PRAGMA( warning( pop ) )
#endif    // USING_CODE_ANALYSIS
#endif    // INTEL_ISPC

int32 CollisionParticlesBVHDepth = 4;
FAutoConsoleVariableRef CVarCollisionParticlesBVHDepth(TEXT("p.CollisionParticlesBVHDepth"), CollisionParticlesBVHDepth, TEXT("The maximum depth for collision particles bvh"));

int32 EnableCollisions = 1;
FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));

int32 ConstraintBPBVHDepth = 2;
FAutoConsoleVariableRef CVarConstraintBPBVHDepth(TEXT("p.ConstraintBPBVHDepth"), ConstraintBPBVHDepth, TEXT("The maximum depth for constraint bvh"));

int32 BPTreeOfGrids = 1;
FAutoConsoleVariableRef CVarBPTreeOfGrids(TEXT("p.BPTreeOfGrids"), BPTreeOfGrids, TEXT("Whether to use a seperate tree of grids for bp"));

extern int32 UseLevelsetCollision;

#if !UE_BUILD_SHIPPING
namespace Chaos
{
	int32 CHAOS_API PendingHierarchyDump = 0;
}
#endif

namespace Chaos
{
template<ECollisionUpdateType, class T_PARTICLES, typename T, int d>
void UpdateConstraintImp(const T_PARTICLES& InParticles, const TImplicitObject<T, d>& ParticleObject, const TRigidTransform<T, d>& ParticleTM, const TImplicitObject<T, d>& LevelsetObject, const TRigidTransform<T, d>& LevelsetTM, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint);

template<typename T, int d>
TPBDCollisionConstraint<T, d>::TPBDCollisionConstraint(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& InPerParticleMaterials, const int32 PairIterations /*= 1*/, const T Thickness /*= (T)0*/)
	: SpatialAccelerationResource(InPlace, InParticles)
	, SpatialAccelerationResource2(InPlace, InParticles, ConstraintBPBVHDepth)
	, MCollided(Collided)
	, MPhysicsMaterials(InPerParticleMaterials)
	, MPairIterations(PairIterations)
	, MThickness(Thickness)
	, MAngularFriction(0)
	, bUseCCD(false)
{
}

template<typename T, int d>
void TPBDCollisionConstraint<T, d>::Reset(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices)
{
	Constraints.Empty();
	MAngularFriction = 0;
	bUseCCD = false;
}

template<typename T, int d>
void TPBDCollisionConstraint<T, d>::UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt)
{
	Reset(InParticles, InIndices);

#if !UE_BUILD_SHIPPING
	if (PendingHierarchyDump)
	{
		ComputeConstraints<true>(InParticles, InIndices, Dt);
	}
	else
#endif
	{
		ComputeConstraints(InParticles, InIndices, Dt);
	}
}


template <typename T, int d>
const ISpatialAcceleration<T, d>& TPBDCollisionConstraint<T, d>::GetSpatialAcceleration() const
{
	if (BPTreeOfGrids)
	{
		return SpatialAccelerationResource2.GetRead();
	}
	else
	{
		return SpatialAccelerationResource.GetRead();
	}
}

template <typename T, int d>
void TPBDCollisionConstraint<T, d>::ReleaseSpatialAcceleration() const
{
	if (BPTreeOfGrids)
	{
		SpatialAccelerationResource2.ReleaseRead();
	}
	else
	{
		SpatialAccelerationResource.ReleaseRead();
	}
}

template <typename T, int d>
void TPBDCollisionConstraint<T, d>::SwapSpatialAcceleration()
{
	if (BPTreeOfGrids)
	{
		SpatialAccelerationResource2.Swap();
	}
	else
	{
		SpatialAccelerationResource.Swap();
	}
}

#if !UE_BUILD_SHIPPING

int32 BucketSizes[] = { 0,1,4,8,16,32,64,128,512,MAX_int32 };


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
	int32 BucketCount[ARRAY_COUNT(BucketSizes)];
	int32 MaxCount;

	FStatHelper()
	{
		FMemory::Memset(BucketCount, 0, sizeof(BucketCount));
		MaxCount = 0;
	}

	void Record(int32 Count)
	{
		for (int32 BucketIdx = 1; BucketIdx < ARRAY_COUNT(BucketSizes); ++BucketIdx)
		{
			if (Count >= BucketSizes[BucketIdx - 1] && Count < BucketSizes[BucketIdx])
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
		for (int32 Idx = 1; Idx < ARRAY_COUNT(BucketSizes); ++Idx)
		{
			int32 NumChars = BucketCount[Idx] / CountPerChar;
			if (Idx < ARRAY_COUNT(BucketSizes) - 1)
			{
				OutLog += FString::Printf(TEXT("\t[%4d - %4d) (%4d) |"), BucketSizes[Idx - 1], BucketSizes[Idx], BucketCount[Idx]);
			}
			else
			{
				OutLog += FString::Printf(TEXT("\t[%4d -  inf) (%4d) |"), BucketSizes[Idx - 1], BucketCount[Idx]);
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

DECLARE_CYCLE_STAT(TEXT("ComputeConstraints"), STAT_ComputeConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("ComputeConstraintsNP"), STAT_ComputeConstraintsNP, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("ComputeConstraintsNP2"), STAT_ComputeConstraintsNP2, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("ComputeConstraintsNP3"), STAT_ComputeConstraintsNP3, STATGROUP_Chaos);

int32 ComputeConstraintsUseAny = 1;
FAutoConsoleVariableRef CVarComputeConstraintsUseAny(TEXT("p.ComputeConstraintsUseAny"), ComputeConstraintsUseAny, TEXT(""));

template <typename T, int d>
template <typename SPATIAL_ACCELERATION, bool bGatherStats>
void TPBDCollisionConstraint<T, d>::ComputeConstraintsHelper(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt, const SPATIAL_ACCELERATION& SpatialAcceleration)
{
	SCOPE_CYCLE_COUNTER(STAT_ComputeConstraints);
	CHAOS_SCOPED_TIMER(ComputeConstraints);
	if (!EnableCollisions) return;
	// Broad phase

#if !UE_BUILD_SHIPPING
	FStatHelper<bGatherStats> BroadphasePotentials;
	FStatHelper<bGatherStats> NarrowPhaseSkipped;
	FStatHelper<bGatherStats> NarrowPhasePerformed;
	FStatHelper<bGatherStats> NarrowPhaseRejected;
	FStatHelper<bGatherStats> BoundsDistribution;
	int32 SimulatedParticles = 0;
#endif

	{
		CHAOS_SCOPED_TIMER(ComputeConstraintsBP);
		SpatialAcceleration.Reinitialize((const TArray<uint32>&)InIndices, true, Dt * BoundsThicknessMultiplier); //todo(ocohen): should we pass MThickness into this structure?
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeConstraintsNP);


		// Narrow phase
		CHAOS_SCOPED_TIMER(ComputeConstraints_NP);

		TQueue<TRigidBodyContactConstraint<T, d>, EQueueMode::Mpsc> Queue;	//todo(ocohen): use per thread buffer instead, need better support than ParallelFor for this
		PhysicsParallelFor(InIndices.Num(), [&](int32 ActiveIdxIdx)
		{
			const int32 Body1Index = InIndices[ActiveIdxIdx];
			if (!ensure(!InParticles.Disabled(Body1Index)))
			{
				//never seen this case but just in case since refactor is fresh. If no one hits this ensure just remove the branch
				return;
			}

			if (InParticles.InvM(Body1Index) == 0)
			{
				return;
			}

#if !UE_BUILD_SHIPPING
			if (bGatherStats)
			{
				++SimulatedParticles;
			}
#endif

			//SCOPE_CYCLE_COUNTER(STAT_ComputeConstraintsNP2);

			TArray<int32> PotentialIntersections;

			const bool bBody1Bounded = HasBoundingBox(InParticles, Body1Index);

			TBox<T, d> Box1;
			const T Box1Thickness = ComputeThickness(InParticles, Dt, Body1Index).Size();
			if (bBody1Bounded)
			{
				Box1 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body1Index);
#if !UE_BUILD_SHIPPING
				if (bGatherStats)
				{
					//const TBox<T, d> Box1 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body1Index);

					BoundsDistribution.Record(Box1.Extents().GetMax());
				}
#endif

				PotentialIntersections = SpatialAcceleration.FindAllIntersections(InParticles, Body1Index);
			}
			else
			{
				PotentialIntersections = SpatialAcceleration.GlobalObjects();
			}

#if !UE_BUILD_SHIPPING
			BroadphasePotentials.Record(PotentialIntersections.Num());

			int32 CountNP = 0;
			int32 RejectedNP = 0;
#endif
			const int32 NumPotentials = PotentialIntersections.Num();
			for (int32 i = 0; i < NumPotentials; ++i)
			{
				const int32 Body2Index = PotentialIntersections[i];
				// Collision group culling...
				// CollisionGroup == 0 : Collide_With_Everything
				// CollisionGroup == INDEX_NONE : Disabled collisions
				// CollisionGroup_A != CollisionGroup_B : Skip Check
				if(InParticles.Disabled(Body2Index))
				{
					continue;
				}
				
				if (InParticles.CollisionGroup(Body1Index) == INDEX_NONE || InParticles.CollisionGroup(Body2Index) == INDEX_NONE)
				{
					continue;
				}
				if (InParticles.CollisionGroup(Body1Index) && InParticles.CollisionGroup(Body2Index) && InParticles.CollisionGroup(Body1Index) != InParticles.CollisionGroup(Body2Index))
				{
					continue;
				}

				if (!InParticles.Geometry(Body1Index) && !InParticles.Geometry(Body2Index))
				{
					continue;
				}

				const bool bBody2Bounded = HasBoundingBox(InParticles, Body2Index);

				if (Body1Index == Body2Index || ((bBody1Bounded == bBody2Bounded) && InParticles.InvM(Body1Index) && InParticles.InvM(Body2Index) && Body2Index > Body1Index))
				{
					//if both are dynamic, assume index order matters
					continue;
				}

				if (bBody1Bounded && bBody2Bounded)
				{
					const TBox<T, d>& Box2 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body2Index);

					if (!Box1.Intersects(Box2))
					{
						continue;
					}
				}
				//				++TotalTested;

				const TVector<T, d> Box2Thickness = ComputeThickness(InParticles, Dt, Body2Index);
				const T UseThickness = FMath::Max(Box1Thickness, Box2Thickness.Size());// + MThickness

				auto Constraint = ComputeConstraint(InParticles, Body1Index, Body2Index, UseThickness);

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
					if (ComputeConstraintsUseAny)
					{
						UpdateConstraint<ECollisionUpdateType::Any>(InParticles, UseThickness, Constraint);
					}
					else
					{
						UpdateConstraint<ECollisionUpdateType::Deepest>(InParticles, UseThickness, Constraint);
					}

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
		}, bGatherStats);
		
		while(!Queue.IsEmpty())
		{				
			int32 Idx = Constraints.AddUninitialized(1);
			Queue.Dequeue(Constraints[Idx]);
		}
	}

#if !UE_BUILD_SHIPPING
	FString OutLog;
	if (bGatherStats)
	{
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
	}
#endif
	//	if (TotalTested > 0)
	//	{
			//UE_LOG(LogChaos, Warning, TEXT("ComputeConstraints: rejected:%f out of rejected:%f = %.3f"), TotalRejected, TotalTested, TotalRejected / TotalTested);
	//	}
}

template<typename T, int d>
template <bool bGatherStats>
void TPBDCollisionConstraint<T, d>::ComputeConstraints(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt)
{
	if (BPTreeOfGrids)
	{
		ComputeConstraintsHelper<decltype(SpatialAccelerationResource2.GetWritable()), bGatherStats>(InParticles, InIndices, Dt, SpatialAccelerationResource2.GetWritable());
	}
	else
	{
		ComputeConstraintsHelper<decltype(SpatialAccelerationResource.GetWritable()), bGatherStats>(InParticles, InIndices, Dt, SpatialAccelerationResource.GetWritable());
	}
}

template<typename T, int d>
void TPBDCollisionConstraint<T, d>::RemoveConstraints(const TSet<uint32>& RemovedParticles)
{
	SpatialAccelerationResource.GetWritable().RemoveElements(RemovedParticles.Array());
	for (int32 i = 0; i < Constraints.Num(); ++i)
	{
		const auto& Constraint = Constraints[i];
		if (RemovedParticles.Contains(Constraint.ParticleIndex) || RemovedParticles.Contains(Constraint.LevelsetIndex))
		{
			Constraints.RemoveAtSwap(i);
			i--;
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateConstraints"), STAT_UpdateConstraints, STATGROUP_Chaos);

template <typename T, int d>
template<typename SPATIAL_ACCELERATION>
void TPBDCollisionConstraint<T, d>::UpdateConstraintsHelper(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles, SPATIAL_ACCELERATION& SpatialAcceleration)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateConstraints);
	double Time = 0;
	FDurationTimer Timer(Time);

	TArray<uint32> AddedParticlesArray = AddedParticles.Array();
	TArray<uint32> NewActiveIndices = ActiveParticles;
	NewActiveIndices.Append(AddedParticlesArray);

	//
	// Broad phase
	//

	{
		//QUICK_SCOPE_CYCLE_COUNTER(Reinitialize);
		// @todo(mlentine): We only need to construct the hierarchy for the islands we care about
		//TBoundingVolumeHierarchy<TPBDRigidParticles<T, d>, TArray<int32>, T, d> LocalHierarchy(InParticles, ActiveParticles, true, Dt * BoundsThicknessMultiplier); 	//todo(ocohen): should we pass MThickness into this structure?
		SpatialAcceleration.Reinitialize((const TArray<uint32>&)NewActiveIndices, true, Dt * BoundsThicknessMultiplier); //todo: faster path when adding just a few
		Timer.Stop();
		UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Construct Hierarchy %f"), Time);
	}

	//
	// Narrow phase
	//

	FCriticalSection CriticalSection;
	Time = 0;
	Timer.Start();
	
	TQueue<TRigidBodyContactConstraint<T, d>, EQueueMode::Mpsc> Queue;	//todo(ocohen): use per thread buffer instead, need better support than ParallelFor for this
	//SpatialAcceleration.AddElements(AddedParticlesArray);	not supported by bvh so just reinitializing. Should probably improve this later
	PhysicsParallelFor(AddedParticlesArray.Num(), [&](int32 Index) {
		int32 Body1Index = AddedParticlesArray[Index];
		if (InParticles.Disabled(Body1Index))
		{
			return;
		}
		if (InParticles.InvM(Body1Index) == 0)
		{
			return;
		}
		TArray<int32> PotentialIntersections;
		TBox<T, d> Box1;
		T Box1Thickness = (T)0;

		const bool bBody1Bounded = HasBoundingBox(InParticles, Body1Index);
		if (bBody1Bounded)
		{
			Box1 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body1Index);
			Box1Thickness = ComputeThickness(InParticles, Dt, Body1Index).Size();
			PotentialIntersections = SpatialAcceleration.FindAllIntersections(Box1);
		}
		else
		{
			PotentialIntersections = SpatialAcceleration.GlobalObjects();
		}
		for (int32 i = 0; i < PotentialIntersections.Num(); ++i)
		{
			int32 Body2Index = PotentialIntersections[i];
			const bool bBody2Bounded = HasBoundingBox(InParticles, Body2Index);

			if(InParticles.Disabled(Body2Index))
			{
				// Can't collide with disabled objects
				continue;
			}

			if (Body1Index == Body2Index || ((bBody1Bounded == bBody2Bounded) && AddedParticles.Contains(Body2Index) && AddedParticles.Contains(Body1Index) && Body2Index > Body1Index))
			{
				continue;
			}

			if (InParticles.InvM(Body1Index) && InParticles.InvM(Body2Index) && (InParticles.Island(Body1Index) != InParticles.Island(Body2Index)))	//todo(ocohen): this is a hack - we should not even consider dynamics from other islands
			{
				continue;
			}

			if (!InParticles.Geometry(Body1Index) && !InParticles.Geometry(Body2Index))
			{
				continue;
			}

			if (bBody1Bounded && bBody2Bounded)
			{	
				const TBox<T, d>& Box2 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body2Index);
				if (!Box1.Intersects(Box2))
				{
					continue;
				}
			}

			//todo(ocohen): this should not be needed in theory, but in practice we accidentally merge islands. We should be doing this test within an island for clusters
			if (InParticles.Island(Body1Index) >= 0 && InParticles.Island(Body2Index) >= 0 && InParticles.Island(Body1Index) != InParticles.Island(Body2Index))
			{
				continue;
			}

			const TVector<T, d> Box2Thickness = ComputeThickness(InParticles, Dt, Body2Index);
			const T UseThickness = FMath::Max(Box1Thickness, Box2Thickness.Size());// + MThickness

			auto Constraint = ComputeConstraint(InParticles, Body1Index, Body2Index, UseThickness);

			//if (true || !InParticles.Geometry(Body1Index)->HasBoundingBox() || !InParticles.Geometry(Body2Index)->HasBoundingBox())
			{
				//use narrow phase to determine if constraint is needed. Without this we can't do shock propagation
				if (ComputeConstraintsUseAny)
				{
					UpdateConstraint<ECollisionUpdateType::Any>(InParticles, UseThickness, Constraint);
				}
				else
				{
					UpdateConstraint<ECollisionUpdateType::Deepest>(InParticles, UseThickness, Constraint);
				}
				if (Constraint.Phi < UseThickness)
				{
					Queue.Enqueue(Constraint);
				}
			}
			/*else
			{
			CriticalSection.Lock();
			Constraints.Add(Constraint);
			CriticalSection.Unlock();
			}*/
		}
	});
	while(!Queue.IsEmpty())
	{				
		int32 Idx = Constraints.AddUninitialized(1);
		Queue.Dequeue(Constraints[Idx]);
	}

	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Update %d Constraints with Potential Collisions %f"), Constraints.Num(), Time);
}

DECLARE_CYCLE_STAT(TEXT("Reconcile Updated Constraints"), STAT_ReconcileConstraints, STATGROUP_Chaos);
template<typename T, int d>
void TPBDCollisionConstraint<T, d>::UpdateConstraints(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles)
{
	{
		SCOPE_CYCLE_COUNTER(STAT_ReconcileConstraints);

		// Updating post-clustering, we will have invalid constraints
		int32 NumRemovedConstraints = 0;
		for(int32 i = 0; i < Constraints.Num(); ++i)
		{
			const FRigidBodyContactConstraint& Constraint = Constraints[i];
			if(InParticles.Disabled(Constraint.ParticleIndex) || InParticles.Disabled(Constraint.LevelsetIndex))
			{
				Constraints.RemoveAtSwap(i);
				++NumRemovedConstraints;
				i--;
			}
		}

		if(NumRemovedConstraints > 0)
		{
			UE_LOG(LogChaos, Verbose, TEXT("TPBDCollisionConstraint::UpdateConstraints - Needed to remove %d constraints because they contained disabled particles."), NumRemovedConstraints);
		}
	}

	if (BPTreeOfGrids)
	{
		UpdateConstraintsHelper(InParticles, InIndices, Dt, AddedParticles, ActiveParticles, SpatialAccelerationResource2.GetWritable());
	}
	else
	{
		UpdateConstraintsHelper(InParticles, InIndices, Dt, AddedParticles, ActiveParticles, SpatialAccelerationResource.GetWritable());
	}
}

template<typename T>
PMatrix<T, 3, 3> ComputeFactorMatrix(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im)
{
	// Rigid objects rotational contribution to the impulse.
	// Vx*M*VxT+Im
	check(Im > FLT_MIN)
	return PMatrix<T, 3, 3>(
		-V[2] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]) + V[1] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]) + Im,
		V[2] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) - V[0] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]),
		-V[1] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) + V[0] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]),
		V[2] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) - V[0] * (V[2] * M.M[2][0] - V[0] * M.M[2][2]) + Im,
		-V[1] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) + V[0] * (V[2] * M.M[1][0] - V[0] * M.M[2][1]),
		-V[1] * (-V[1] * M.M[0][0] + V[0] * M.M[1][0]) + V[0] * (-V[1] * M.M[1][0] + V[0] * M.M[1][1]) + Im);
}

template<typename T, int d>
TVector<T, d> GetEnergyClampedImpulse(const TPBDRigidParticles<T, d>& InParticles, const TRigidBodyContactConstraint<T, d>& Constraint, const TVector<T, d>& Impulse, const TVector<T, d>& VectorToPoint1, const TVector<T, d>& VectorToPoint2, const TVector<T, d>& Velocity1, const TVector<T, d>& Velocity2)
{
	TVector<T, d> Jr0, Jr1, IInvJr0, IInvJr1;
	T ImpulseRatioNumerator0 = 0, ImpulseRatioNumerator1 = 0, ImpulseRatioDenom0 = 0, ImpulseRatioDenom1 = 0;
	T ImpulseSize = Impulse.SizeSquared();
	TVector<T, d> KinematicVelocity = !InParticles.InvM(Constraint.ParticleIndex) ? Velocity1 : !InParticles.InvM(Constraint.LevelsetIndex) ? Velocity2 : TVector<T, d>(0);
	if (InParticles.InvM(Constraint.ParticleIndex))
	{
		Jr0 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
		IInvJr0 = InParticles.Q(Constraint.ParticleIndex).RotateVector(InParticles.InvI(Constraint.ParticleIndex) * InParticles.Q(Constraint.ParticleIndex).UnrotateVector(Jr0));
		ImpulseRatioNumerator0 = TVector<T, d>::DotProduct(Impulse, InParticles.V(Constraint.ParticleIndex) - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr0, InParticles.W(Constraint.ParticleIndex));
		ImpulseRatioDenom0 = ImpulseSize / InParticles.M(Constraint.ParticleIndex) + TVector<T, d>::DotProduct(Jr0, IInvJr0);
	}
	if (InParticles.InvM(Constraint.LevelsetIndex))
	{
		Jr1 = TVector<T, d>::CrossProduct(VectorToPoint2, Impulse);
		IInvJr1 = InParticles.Q(Constraint.LevelsetIndex).RotateVector(InParticles.InvI(Constraint.LevelsetIndex) * InParticles.Q(Constraint.LevelsetIndex).UnrotateVector(Jr1));
		ImpulseRatioNumerator1 = TVector<T, d>::DotProduct(Impulse, InParticles.V(Constraint.LevelsetIndex) - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr1, InParticles.W(Constraint.LevelsetIndex));
		ImpulseRatioDenom1 = ImpulseSize / InParticles.M(Constraint.LevelsetIndex) + TVector<T, d>::DotProduct(Jr1, IInvJr1);
	}
	T Numerator = -2 * (ImpulseRatioNumerator0 - ImpulseRatioNumerator1);
	if (Numerator < 0)
	{
		return TVector<T, d>(0);
	}
	check(Numerator >= 0);
	T Denominator = ImpulseRatioDenom0 + ImpulseRatioDenom1;
	return Numerator < Denominator ? (Impulse * Numerator / Denominator) : Impulse;
}

DECLARE_CYCLE_STAT(TEXT("Apply"), STAT_Apply, STATGROUP_ChaosWide);
template<typename T, int d>
void TPBDCollisionConstraint<T, d>::Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices)
{
	PhysicsParallelFor(InConstraintIndices.Num(), [&](int32 ConstraintIndex) {
		FRigidBodyContactConstraint& Constraint = Constraints[InConstraintIndices[ConstraintIndex]];
		if (InParticles.Sleeping(Constraint.ParticleIndex))
		{
			ensure((InParticles.Sleeping(Constraint.LevelsetIndex) || InParticles.InvM(Constraint.LevelsetIndex) == 0));
			return;
		}
		if (InParticles.Sleeping(Constraint.LevelsetIndex))
		{
			ensure((InParticles.Sleeping(Constraint.ParticleIndex) || InParticles.InvM(Constraint.ParticleIndex) == 0));
			return;
		}
		const_cast<TPBDCollisionConstraint<T, d>*>(this)->UpdateConstraint<ECollisionUpdateType::Deepest>(InParticles, MThickness, Constraint);
		if (Constraint.Phi >= MThickness)
		{
			return;
		}
		MCollided[Constraint.LevelsetIndex] = true;
		MCollided[Constraint.ParticleIndex] = true;
		TVector<T, d> VectorToPoint1 = Constraint.Location - InParticles.P(Constraint.ParticleIndex);
		TVector<T, d> VectorToPoint2 = Constraint.Location - InParticles.P(Constraint.LevelsetIndex);
		TVector<T, d> Body1Velocity = InParticles.V(Constraint.ParticleIndex) + TVector<T, d>::CrossProduct(InParticles.W(Constraint.ParticleIndex), VectorToPoint1);
		TVector<T, d> Body2Velocity = InParticles.V(Constraint.LevelsetIndex) + TVector<T, d>::CrossProduct(InParticles.W(Constraint.LevelsetIndex), VectorToPoint2);
		TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
		if (TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) < 0) // ignore separating constraints
		{
			PMatrix<T, d, d> WorldSpaceInvI1 = (InParticles.Q(Constraint.ParticleIndex) * FMatrix::Identity).GetTransposed() * InParticles.InvI(Constraint.ParticleIndex) * (InParticles.Q(Constraint.ParticleIndex) * FMatrix::Identity);
			PMatrix<T, d, d> WorldSpaceInvI2 = (InParticles.Q(Constraint.LevelsetIndex) * FMatrix::Identity).GetTransposed() * InParticles.InvI(Constraint.LevelsetIndex) * (InParticles.Q(Constraint.LevelsetIndex) * FMatrix::Identity);
			PMatrix<T, d, d> Factor =
				(InParticles.InvM(Constraint.ParticleIndex) > FLT_MIN ? ComputeFactorMatrix(VectorToPoint1, WorldSpaceInvI1, InParticles.InvM(Constraint.ParticleIndex)) : PMatrix<T, d, d>(0)) +
				(InParticles.InvM(Constraint.LevelsetIndex) > FLT_MIN ? ComputeFactorMatrix(VectorToPoint2, WorldSpaceInvI2, InParticles.InvM(Constraint.LevelsetIndex)) : PMatrix<T, d, d>(0));
			TVector<T, d> Impulse;
			TVector<T, d> AngularImpulse(0);

			// Resting contact if very close to the surface
			T Restitution = (T)0;
			T Friction = (T)0;
			bool bApplyRestitution = (RelativeVelocity.Size() > (2 * 980 * Dt));
			if (MPhysicsMaterials[Constraint.LevelsetIndex] && MPhysicsMaterials[Constraint.ParticleIndex])
			{
				if (bApplyRestitution)
				{
					Restitution = FMath::Min(MPhysicsMaterials[Constraint.ParticleIndex]->Restitution, MPhysicsMaterials[Constraint.LevelsetIndex]->Restitution);
				}
				Friction = FMath::Max(MPhysicsMaterials[Constraint.ParticleIndex]->Friction, MPhysicsMaterials[Constraint.LevelsetIndex]->Friction);
			}
			else if (MPhysicsMaterials[Constraint.ParticleIndex])
			{
				if (bApplyRestitution)
				{
					Restitution = MPhysicsMaterials[Constraint.ParticleIndex]->Restitution;
				}
				Friction = MPhysicsMaterials[Constraint.ParticleIndex]->Friction;
			}
			else if (MPhysicsMaterials[Constraint.LevelsetIndex])
			{
				if (bApplyRestitution)
				{
					Restitution = MPhysicsMaterials[Constraint.LevelsetIndex]->Restitution;
				}
				Friction = MPhysicsMaterials[Constraint.LevelsetIndex]->Friction;
			}

			if (Friction)
			{
				T RelativeNormalVelocity = TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal);
				if (RelativeNormalVelocity > 0)
				{
					RelativeNormalVelocity = 0;
				}
				TVector<T, d> VelocityChange = -(Restitution * RelativeNormalVelocity * Constraint.Normal + RelativeVelocity);
				T NormalVelocityChange = TVector<T, d>::DotProduct(VelocityChange, Constraint.Normal);
				PMatrix<T, d, d> FactorInverse = Factor.Inverse();
				TVector<T, d> MinimalImpulse = FactorInverse * VelocityChange;
				const T MinimalImpulseDotNormal = TVector<T, d>::DotProduct(MinimalImpulse, Constraint.Normal);
				const T TangentialSize = (MinimalImpulse - MinimalImpulseDotNormal * Constraint.Normal).Size();
				if (TangentialSize <= Friction * MinimalImpulseDotNormal)
				{
					//within friction cone so just solve for static friction stopping the object
					Impulse = MinimalImpulse;
					if (MAngularFriction)
					{
						TVector<T, d> RelativeAngularVelocity = InParticles.W(Constraint.ParticleIndex) - InParticles.W(Constraint.LevelsetIndex);
						T AngularNormal = TVector<T, d>::DotProduct(RelativeAngularVelocity, Constraint.Normal);
						TVector<T, d> AngularTangent = RelativeAngularVelocity - AngularNormal * Constraint.Normal;
						TVector<T, d> FinalAngularVelocity = FMath::Sign(AngularNormal) * FMath::Max((T)0, FMath::Abs(AngularNormal) - MAngularFriction * NormalVelocityChange) * Constraint.Normal + FMath::Max((T)0, AngularTangent.Size() - MAngularFriction * NormalVelocityChange) * AngularTangent.GetSafeNormal();
						TVector<T, d> Delta = FinalAngularVelocity - RelativeAngularVelocity;
						if (!InParticles.InvM(Constraint.ParticleIndex))
						{
							PMatrix<T, d, d> WorldSpaceI2 = (InParticles.Q(Constraint.LevelsetIndex) * FMatrix::Identity) * InParticles.I(Constraint.LevelsetIndex) * (InParticles.Q(Constraint.LevelsetIndex) * FMatrix::Identity).GetTransposed();
							TVector<T, d> ImpulseDelta = InParticles.M(Constraint.LevelsetIndex) * TVector<T, d>::CrossProduct(VectorToPoint2, Delta);
							Impulse += ImpulseDelta;
							AngularImpulse += WorldSpaceI2 * Delta - TVector<T, d>::CrossProduct(VectorToPoint2, ImpulseDelta);
						}
						else if (!InParticles.InvM(Constraint.LevelsetIndex))
						{
							PMatrix<T, d, d> WorldSpaceI1 = (InParticles.Q(Constraint.ParticleIndex) * FMatrix::Identity) * InParticles.I(Constraint.ParticleIndex) * (InParticles.Q(Constraint.ParticleIndex) * FMatrix::Identity).GetTransposed();
							TVector<T, d> ImpulseDelta = InParticles.M(Constraint.ParticleIndex) * TVector<T, d>::CrossProduct(VectorToPoint1, Delta);
							Impulse += ImpulseDelta;
							AngularImpulse += WorldSpaceI1 * Delta - TVector<T, d>::CrossProduct(VectorToPoint1, ImpulseDelta);
						}
						else
						{
							PMatrix<T, d, d> Cross1(0, VectorToPoint1.Z, -VectorToPoint1.Y, -VectorToPoint1.Z, 0, VectorToPoint1.X, VectorToPoint1.Y, -VectorToPoint1.X, 0);
							PMatrix<T, d, d> Cross2(0, VectorToPoint2.Z, -VectorToPoint2.Y, -VectorToPoint2.Z, 0, VectorToPoint2.X, VectorToPoint2.Y, -VectorToPoint2.X, 0);
							PMatrix<T, d, d> CrossI1 = Cross1 * WorldSpaceInvI1;
							PMatrix<T, d, d> CrossI2 = Cross2 * WorldSpaceInvI2;
							PMatrix<T, d, d> Diag1 = CrossI1 * Cross1.GetTransposed() + CrossI2 * Cross2.GetTransposed();
							Diag1.M[0][0] += InParticles.InvM(Constraint.ParticleIndex) + InParticles.InvM(Constraint.LevelsetIndex);
							Diag1.M[1][1] += InParticles.InvM(Constraint.ParticleIndex) + InParticles.InvM(Constraint.LevelsetIndex);
							Diag1.M[2][2] += InParticles.InvM(Constraint.ParticleIndex) + InParticles.InvM(Constraint.LevelsetIndex);
							PMatrix<T, d, d> OffDiag1 = (CrossI1 + CrossI2) * -1;
							PMatrix<T, d, d> Diag2 = (WorldSpaceInvI1 + WorldSpaceInvI2).Inverse();
							PMatrix<T, d, d> OffDiag1Diag2 = OffDiag1 * Diag2;
							TVector<T, d> ImpulseDelta = PMatrix<T, d, d>((Diag1 - OffDiag1Diag2 * OffDiag1.GetTransposed()).Inverse()) * ((OffDiag1Diag2 * -1) * Delta);
							Impulse += ImpulseDelta;
							AngularImpulse += Diag2 * (Delta - PMatrix<T, d, d>(OffDiag1.GetTransposed()) * ImpulseDelta);
						}
					}
				}
				else
				{
					//outside friction cone, solve for normal relative velocity and keep tangent at cone edge
					TVector<T, d> Tangent = (RelativeVelocity - TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) * Constraint.Normal).GetSafeNormal();
					TVector<T, d> DirectionalFactor = Factor * (Constraint.Normal - Friction * Tangent);
					T ImpulseDenominator = TVector<T, d>::DotProduct(Constraint.Normal, DirectionalFactor);
					if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nDirectionalFactor:%s, ImpulseDenominator:%f"),
						*Constraint.ToString(),
						*InParticles.ToString(Constraint.ParticleIndex),
						*InParticles.ToString(Constraint.LevelsetIndex),
						*DirectionalFactor.ToString(), ImpulseDenominator))
					{
						ImpulseDenominator = (T)1;
					}

					const T ImpulseMag = -(1 + Restitution) * RelativeNormalVelocity / ImpulseDenominator;
					Impulse = ImpulseMag * (Constraint.Normal - Friction * Tangent);
				}
			}
			else
			{
				T ImpulseDenominator = TVector<T, d>::DotProduct(Constraint.Normal, Factor * Constraint.Normal);
				TVector<T, d> ImpulseNumerator = -(1 + Restitution) * TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) * Constraint.Normal;
				if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
					*Constraint.ToString(),
					*InParticles.ToString(Constraint.ParticleIndex),
					*InParticles.ToString(Constraint.LevelsetIndex),
					*(Factor*Constraint.Normal).ToString(), ImpulseDenominator))
				{
					ImpulseDenominator = (T)1;
				}
				Impulse = ImpulseNumerator / ImpulseDenominator;
			}
			Impulse = GetEnergyClampedImpulse(InParticles, Constraint, Impulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
			Constraint.AccumulatedImpulse += Impulse;
			TVector<T, d> AngularImpulse1 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse) + AngularImpulse;
			TVector<T, d> AngularImpulse2 = TVector<T, d>::CrossProduct(VectorToPoint2, -Impulse) - AngularImpulse;
			// Velocity update for next step
			InParticles.V(Constraint.ParticleIndex) += InParticles.InvM(Constraint.ParticleIndex) * Impulse;
			InParticles.W(Constraint.ParticleIndex) += WorldSpaceInvI1 * AngularImpulse1;
			InParticles.V(Constraint.LevelsetIndex) -= InParticles.InvM(Constraint.LevelsetIndex) * Impulse;
			InParticles.W(Constraint.LevelsetIndex) += WorldSpaceInvI2 * AngularImpulse2;
			// Position update as part of pbd
			InParticles.P(Constraint.ParticleIndex) += (InParticles.InvM(Constraint.ParticleIndex) * Impulse) * Dt;
			InParticles.Q(Constraint.ParticleIndex) += TRotation<T, d>(WorldSpaceInvI1 * AngularImpulse1, 0.f) * InParticles.Q(Constraint.ParticleIndex) * Dt * T(0.5);
			InParticles.Q(Constraint.ParticleIndex).Normalize();
			InParticles.P(Constraint.LevelsetIndex) -= (InParticles.InvM(Constraint.LevelsetIndex) * Impulse) * Dt;
			InParticles.Q(Constraint.LevelsetIndex) += TRotation<T, d>(WorldSpaceInvI2 * AngularImpulse2, 0.f) * InParticles.Q(Constraint.LevelsetIndex) * Dt * T(0.5);
			InParticles.Q(Constraint.LevelsetIndex).Normalize();
		}
	});
}

DECLARE_CYCLE_STAT(TEXT("ApplyPushOut"), STAT_ApplyPushOut, STATGROUP_ChaosWide);
template<typename T, int d>
bool TPBDCollisionConstraint<T, d>::ApplyPushOut(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices, const TSet<int32>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
{
	SCOPE_CYCLE_COUNTER(STAT_ApplyPushOut);

	bool NeedsAnotherIteration = false;

	PhysicsParallelFor(InConstraintIndices.Num(), [&](int32 ConstraintIndex) {
		FRigidBodyContactConstraint& Constraint = Constraints[InConstraintIndices[ConstraintIndex]];
		if (InParticles.Sleeping(Constraint.ParticleIndex))
		{
			ensure((InParticles.Sleeping(Constraint.LevelsetIndex) || InParticles.InvM(Constraint.LevelsetIndex) == 0));
			return;
		}
		if (InParticles.Sleeping(Constraint.LevelsetIndex))
		{
			ensure((InParticles.Sleeping(Constraint.ParticleIndex) || InParticles.InvM(Constraint.ParticleIndex) == 0));
			return;
		}
		for (int32 PairIteration = 0; PairIteration < MPairIterations; ++PairIteration)
		{
			UpdateConstraint<ECollisionUpdateType::Deepest>(InParticles, MThickness, Constraint);
			if (Constraint.Phi >= MThickness)
			{
				break;
			}

			if ((!InParticles.InvM(Constraint.ParticleIndex) || IsTemporarilyStatic.Contains(Constraint.ParticleIndex)) && (!InParticles.InvM(Constraint.LevelsetIndex) || IsTemporarilyStatic.Contains(Constraint.LevelsetIndex)))
			{
				break;
			}

			NeedsAnotherIteration = true;
			PMatrix<T, d, d> WorldSpaceInvI1 = (InParticles.Q(Constraint.ParticleIndex) * FMatrix::Identity).GetTransposed() * InParticles.InvI(Constraint.ParticleIndex) * (InParticles.Q(Constraint.ParticleIndex) * FMatrix::Identity);
			PMatrix<T, d, d> WorldSpaceInvI2 = (InParticles.Q(Constraint.LevelsetIndex) * FMatrix::Identity).GetTransposed() * InParticles.InvI(Constraint.LevelsetIndex) * (InParticles.Q(Constraint.LevelsetIndex) * FMatrix::Identity);
			TVector<T, d> VectorToPoint1 = Constraint.Location - InParticles.P(Constraint.ParticleIndex);
			TVector<T, d> VectorToPoint2 = Constraint.Location - InParticles.P(Constraint.LevelsetIndex);
			PMatrix<T, d, d> Factor =
				((InParticles.InvM(Constraint.ParticleIndex) && !IsTemporarilyStatic.Contains(Constraint.ParticleIndex)) ? ComputeFactorMatrix(VectorToPoint1, WorldSpaceInvI1, InParticles.InvM(Constraint.ParticleIndex)) : PMatrix<T, d, d>(0)) +
				((InParticles.InvM(Constraint.LevelsetIndex) && !IsTemporarilyStatic.Contains(Constraint.LevelsetIndex)) ? ComputeFactorMatrix(VectorToPoint2, WorldSpaceInvI2, InParticles.InvM(Constraint.LevelsetIndex)) : PMatrix<T, d, d>(0));
			T Numerator = FMath::Min((T)(Iteration + 2), (T)NumIterations);
			T ScalingFactor = Numerator / (T)NumIterations;

			//if pushout is needed we better fix relative velocity along normal. Treat it as if 0 restitution
			TVector<T, d> Body1Velocity = InParticles.V(Constraint.ParticleIndex) + TVector<T, d>::CrossProduct(InParticles.W(Constraint.ParticleIndex), VectorToPoint1);
			TVector<T, d> Body2Velocity = InParticles.V(Constraint.LevelsetIndex) + TVector<T, d>::CrossProduct(InParticles.W(Constraint.LevelsetIndex), VectorToPoint2);
			TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
			const T RelativeVelocityDotNormal = TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal);
			if (RelativeVelocityDotNormal < 0)
			{
				T ImpulseDenominator = TVector<T, d>::DotProduct(Constraint.Normal, Factor * Constraint.Normal);
				TVector<T, d> ImpulseNumerator = -TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) * Constraint.Normal * ScalingFactor;
				if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("ApplyPushout Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
					*Constraint.ToString(),
					*InParticles.ToString(Constraint.ParticleIndex),
					*InParticles.ToString(Constraint.LevelsetIndex),
					*(Factor*Constraint.Normal).ToString(), ImpulseDenominator))
				{
					ImpulseDenominator = (T)1;
				}

				TVector<T, d> VelocityFixImpulse = ImpulseNumerator / ImpulseDenominator;
				VelocityFixImpulse = GetEnergyClampedImpulse(InParticles, Constraint, VelocityFixImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
				Constraint.AccumulatedImpulse += VelocityFixImpulse;	//question: should we track this?
				if (!IsTemporarilyStatic.Contains(Constraint.ParticleIndex))
				{
					TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint1, VelocityFixImpulse);
					InParticles.V(Constraint.ParticleIndex) += InParticles.InvM(Constraint.ParticleIndex) * VelocityFixImpulse;
					InParticles.W(Constraint.ParticleIndex) += WorldSpaceInvI1 * AngularImpulse;

				}

				if (!IsTemporarilyStatic.Contains(Constraint.LevelsetIndex))
				{
					TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint2, -VelocityFixImpulse);
					InParticles.V(Constraint.LevelsetIndex) -= InParticles.InvM(Constraint.LevelsetIndex) * VelocityFixImpulse;
					InParticles.W(Constraint.LevelsetIndex) += WorldSpaceInvI2 * AngularImpulse;
				}

			}


			TVector<T, d> Impulse = PMatrix<T, d, d>(Factor.Inverse()) * ((-Constraint.Phi + MThickness) * ScalingFactor * Constraint.Normal);
			TVector<T, d> AngularImpulse1 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
			TVector<T, d> AngularImpulse2 = TVector<T, d>::CrossProduct(VectorToPoint2, -Impulse);
			if (!IsTemporarilyStatic.Contains(Constraint.ParticleIndex))
			{
				InParticles.P(Constraint.ParticleIndex) += InParticles.InvM(Constraint.ParticleIndex) * Impulse;
				InParticles.Q(Constraint.ParticleIndex) = TRotation<T, d>::FromVector(WorldSpaceInvI1 * AngularImpulse1) * InParticles.Q(Constraint.ParticleIndex);
				InParticles.Q(Constraint.ParticleIndex).Normalize();
			}
			if (!IsTemporarilyStatic.Contains(Constraint.LevelsetIndex))
			{
				InParticles.P(Constraint.LevelsetIndex) -= InParticles.InvM(Constraint.LevelsetIndex) * Impulse;
				InParticles.Q(Constraint.LevelsetIndex) = TRotation<T, d>::FromVector(WorldSpaceInvI2 * AngularImpulse2) * InParticles.Q(Constraint.LevelsetIndex);
				InParticles.Q(Constraint.LevelsetIndex).Normalize();
			}
		}
	});

	return NeedsAnotherIteration;
}

template<typename T, int d>
bool TPBDCollisionConstraint<T, d>::NearestPoint(TArray<Pair<TVector<T, d>, TVector<T, d>>>& Points, TVector<T, d>& Direction, TVector<T, d>& ClosestPoint)
{
	check(Points.Num() > 1 && Points.Num() <= 4);
	if (Points.Num() == 2)
	{
		TPlane<T, d> LocalPlane(Points[1].First, Points[0].First - Points[1].First);
		TVector<T, d> Normal;
		const auto& Phi = LocalPlane.PhiWithNormal(TVector<T, d>(0), Normal);
		if ((TVector<T, d>::DotProduct(-Points[1].First, Normal.GetSafeNormal()) - Points[1].First.Size()) < SMALL_NUMBER)
		{
			T Alpha = Points[0].First.Size() / (Points[1].First - Points[0].First).Size();
			ClosestPoint = (1 - Alpha) * Points[0].Second + Alpha * Points[1].Second;
			return true;
		}
		if (Phi > 0)
		{
			check(Points.Num() == 2);
			Direction = TVector<T, d>::CrossProduct(TVector<T, d>::CrossProduct(Normal, -Points[1].First), Normal);
		}
		else
		{
			Direction = -Points[1].First;
			Points.RemoveAtSwap(0);
			check(Points.Num() == 1);
		}
		check(Points.Num() > 1 && Points.Num() < 4);
		return false;
	}
	if (Points.Num() == 3)
	{
		TVector<T, d> TriangleNormal = TVector<T, d>::CrossProduct(Points[0].First - Points[2].First, Points[0].First - Points[1].First);
		TPlane<T, d> LocalPlane1(Points[2].First, TVector<T, d>::CrossProduct(Points[0].First - Points[2].First, TriangleNormal));
		TPlane<T, d> LocalPlane2(Points[2].First, TVector<T, d>::CrossProduct(Points[1].First - Points[2].First, TriangleNormal));
		TVector<T, d> Normal;
		T Phi = LocalPlane1.PhiWithNormal(TVector<T, d>(0), Normal);
		if (Phi > 0)
		{
			TVector<T, d> Delta = Points[0].First - Points[2].First;
			if (TVector<T, d>::DotProduct(-Points[2].First, Delta) > 0)
			{
				Direction = TVector<T, d>::CrossProduct(TVector<T, d>::CrossProduct(Delta, -Points[2].First), Delta);
				Points.RemoveAtSwap(1);
				check(Points.Num() == 2);
			}
			else
			{
				Delta = Points[1].First - Points[2].First;
				if (TVector<T, d>::DotProduct(-Points[2].First, Delta) > 0)
				{
					Direction = TVector<T, d>::CrossProduct(TVector<T, d>::CrossProduct(Delta, -Points[2].First), Delta);
					Points.RemoveAtSwap(0);
					check(Points.Num() == 2);
				}
				else
				{
					Direction = -Points[2].First;
					Points = {Points[2]};
					check(Points.Num() == 1);
				}
			}
		}
		else
		{
			Phi = LocalPlane2.PhiWithNormal(TVector<T, d>(0), Normal);
			if (Phi > 0)
			{
				TVector<T, d> Delta = Points[1].First - Points[2].First;
				if (TVector<T, d>::DotProduct(-Points[2].First, Delta) > 0)
				{
					Direction = TVector<T, d>::CrossProduct(TVector<T, d>::CrossProduct(Delta, -Points[2].First), Delta);
					Points.RemoveAtSwap(0);
					check(Points.Num() == 2);
				}
				else
				{
					Direction = -Points[2].First;
					Points = {Points[2]};
					check(Points.Num() == 1);
				}
			}
			else
			{
				const auto DotResult = TVector<T, d>::DotProduct(TriangleNormal, -Points[2].First);
				// We are inside the triangle
				if (DotResult < SMALL_NUMBER)
				{
					TVector<T, 3> Bary;
					TVector<T, d> P10 = Points[1].First - Points[0].First;
					TVector<T, d> P20 = Points[2].First - Points[0].First;
					TVector<T, d> PP0 = -Points[0].First;
					T Size10 = P10.SizeSquared();
					T Size20 = P20.SizeSquared();
					T ProjSides = TVector<T, d>::DotProduct(P10, P20);
					T ProjP1 = TVector<T, d>::DotProduct(PP0, P10);
					T ProjP2 = TVector<T, d>::DotProduct(PP0, P20);
					T Denom = Size10 * Size20 - ProjSides * ProjSides;
					Bary.Y = (Size20 * ProjP1 - ProjSides * ProjP2) / Denom;
					Bary.Z = (Size10 * ProjP2 - ProjSides * ProjP1) / Denom;
					Bary.X = 1.0f - Bary.Z - Bary.Y;
					ClosestPoint = Points[0].Second * Bary.X + Points[1].Second * Bary.Y + Points[2].Second * Bary.Z;
					return true;
				}
				if (DotResult > 0)
				{
					Direction = TriangleNormal;
				}
				else
				{
					Direction = -TriangleNormal;
					Points.Swap(0, 1);
					check(Points.Num() == 3);
				}
			}
		}
		check(Points.Num() > 0 && Points.Num() < 4);
		return false;
	}
	if (Points.Num() == 4)
	{
		TVector<T, d> TriangleNormal = TVector<T, d>::CrossProduct(Points[1].First - Points[3].First, Points[1].First - Points[2].First);
		if (TVector<T, d>::DotProduct(TriangleNormal, Points[0].First - Points[3].First) > 0)
		{
			TriangleNormal *= -1;
		}
		T DotResult = TVector<T, d>::DotProduct(TriangleNormal, -Points[3].First);
		if (DotResult > 0)
		{
			Points = {Points[1], Points[2], Points[3]};
			check(Points.Num() == 3);
			return NearestPoint(Points, Direction, ClosestPoint);
		}
		TriangleNormal = TVector<T, d>::CrossProduct(Points[2].First - Points[0].First, Points[2].First - Points[3].First);
		if (TVector<T, d>::DotProduct(TriangleNormal, Points[1].First - Points[3].First) > 0)
		{
			TriangleNormal *= -1;
		}
		DotResult = TVector<T, d>::DotProduct(TriangleNormal, -Points[3].First);
		if (DotResult > 0)
		{
			Points = {Points[0], Points[2], Points[3]};
			check(Points.Num() == 3);
			return NearestPoint(Points, Direction, ClosestPoint);
		}
		TriangleNormal = TVector<T, d>::CrossProduct(Points[3].First - Points[1].First, Points[3].First - Points[0].First);
		if (TVector<T, d>::DotProduct(TriangleNormal, Points[2].First - Points[3].First) > 0)
		{
			TriangleNormal *= -1;
		}
		DotResult = TVector<T, d>::DotProduct(TriangleNormal, -Points[3].First);
		if (DotResult > 0)
		{
			Points = {Points[0], Points[1], Points[3]};
			check(Points.Num() == 3);
			return NearestPoint(Points, Direction, ClosestPoint);
		}
		TVector<T, 4> Bary;
		TVector<T, d> PP0 = -Points[0].First;
		TVector<T, d> PP1 = -Points[1].First;
		TVector<T, d> P10 = Points[1].First - Points[0].First;
		TVector<T, d> P20 = Points[2].First - Points[0].First;
		TVector<T, d> P30 = Points[3].First - Points[0].First;
		TVector<T, d> P21 = Points[2].First - Points[1].First;
		TVector<T, d> P31 = Points[3].First - Points[1].First;
		Bary[0] = TVector<T, d>::DotProduct(PP1, TVector<T, d>::CrossProduct(P31, P21));
		Bary[1] = TVector<T, d>::DotProduct(PP0, TVector<T, d>::CrossProduct(P20, P30));
		Bary[2] = TVector<T, d>::DotProduct(PP0, TVector<T, d>::CrossProduct(P30, P10));
		Bary[3] = TVector<T, d>::DotProduct(PP0, TVector<T, d>::CrossProduct(P10, P20));
		T Denom = TVector<T, d>::DotProduct(P10, TVector<T, d>::CrossProduct(P20, P30));
		ClosestPoint = (Bary[0] * Points[0].Second + Bary[1] * Points[1].Second + Bary[2] * Points[2].Second + Bary[3] * Points[3].Second) / Denom;
		return true;
	}
	check(Points.Num() > 1 && Points.Num() < 4);
	return false;
}

template<class T_PARTICLES, typename T, int d>
TVector<T, d> GetPosition(const T_PARTICLES& InParticles)
{
	check(false);
	return TVector<T, d>();
}

template<typename T, int d>
TVector<T, d> GetPosition(const TParticles<T, d>& InParticles, const int32 Index)
{
	return InParticles.X(Index);
}

template<typename T, int d>
TVector<T, d> GetPosition(const TPBDRigidParticles<T, d>& InParticles, const int32 Index)
{
	return InParticles.P(Index);
}

template<class T_PARTICLES, typename T, int d>
TRotation<T, d> GetRotation(const T_PARTICLES& InParticles)
{
	check(false);
	return TRotation<T, d>();
}

template<typename T, int d>
TRotation<T, d> GetRotation(const TRigidParticles<T, d>& InParticles, const int32 Index)
{
	return InParticles.R(Index);
}

template<typename T, int d>
TRotation<T, d> GetRotation(const TPBDRigidParticles<T, d>& InParticles, const int32 Index)
{
	return InParticles.Q(Index);
}

template<class T_PARTICLES, typename T, int d>
TRigidTransform<T, d> GetTransform(const T_PARTICLES& InParticles)
{
	check(false);
	return TRigidTransform<T, d>();
}

template<typename T, int d>
TRigidTransform<T, d> GetTransform(const TRigidParticles<T, d>& InParticles, const int32 Index)
{
	return TRigidTransform<T, d>(InParticles.X(Index), InParticles.R(Index));
}

template<typename T, int d>
TRigidTransform<T, d> GetTransform(const TPBDRigidParticles<T, d>& InParticles, const int32 Index)
{
	return TRigidTransform<T, d>(InParticles.P(Index), InParticles.Q(Index));
}

#if 0
template<typename T, int d>
void UpdateLevelsetConstraintHelperCCD(const TRigidParticles<T, d>& InParticles, const int32 j, const TRigidTransform<T, d>& LocalToWorld1, const TRigidTransform<T, d>& LocalToWorld2, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	if (InParticles.CollisionParticles(Constraint.ParticleIndex))
	{
		const TRigidTransform<T, d> PreviousLocalToWorld1 = GetTransform(InParticles, Constraint.ParticleIndex);
		TVector<T, d> WorldSpacePointStart = PreviousLocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		TVector<T, d> WorldSpacePointEnd = LocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		TVector<T, d> Body2SpacePointStart = LocalToWorld2.InverseTransformPosition(WorldSpacePointStart);
		TVector<T, d> Body2SpacePointEnd = LocalToWorld2.InverseTransformPosition(WorldSpacePointEnd);
		Pair<TVector<T, d>, bool> PointPair = InParticles.Geometry(Constraint.LevelsetIndex)->FindClosestIntersection(Body2SpacePointStart, Body2SpacePointEnd, Thickness);
		if (PointPair.Second)
		{
			const TVector<T, d> WorldSpaceDelta = WorldSpacePointEnd - TVector<T, d>(LocalToWorld2.TransformPosition(PointPair.First));
			Constraint.Phi = -WorldSpaceDelta.Size();
			Constraint.Normal = LocalToWorld2.TransformVector(InParticles.Geometry(Constraint.LevelsetIndex)->Normal(PointPair.First));
			// @todo(mlentine): Should we be using the actual collision point or that point evolved to the current time step?
			Constraint.Location = WorldSpacePointEnd;
		}
	}
}
#endif

template <typename T, int d>
bool SampleObjectHelper(const TImplicitObject<T, d>& Object, const TRigidTransform<T,d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
	TVector<T, d> LocalNormal;
	T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
	if (LocalPhi < Constraint.Phi)
	{
		Constraint.Phi = LocalPhi;
		Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
		Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
		return true;
	}
	return false;
}

template <typename T, int d>
bool SampleObjectNoNormal(const TImplicitObject<T, d>& Object, const TRigidTransform<T,d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
	TVector<T, d> LocalNormal;
	T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
	if (LocalPhi < Constraint.Phi)
	{
		Constraint.Phi = LocalPhi;
		return true;
	}
	return false;
}

template <typename T, int d>
bool SampleObjectNormalAverageHelper(const TImplicitObject<T, d>& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, T& TotalThickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
	TVector<T, d> LocalNormal;
	T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
	T LocalThickness = LocalPhi - Thickness;
	if (LocalThickness < -KINDA_SMALL_NUMBER)
	{
		Constraint.Location += LocalPoint * LocalThickness;
		TotalThickness += LocalThickness;
		return true;
	}
	return false;
}

DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetPartial"), STAT_UpdateLevelsetPartial, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetFindParticles"), STAT_UpdateLevelsetFindParticles, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetBVHTraversal"), STAT_UpdateLevelsetBVHTraversal, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetSignedDistance"), STAT_UpdateLevelsetSignedDistance, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetAll"), STAT_UpdateLevelsetAll, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("SampleObject"), STAT_SampleObject, STATGROUP_ChaosWide);

int32 NormalAveraging = 1;
FAutoConsoleVariableRef CVarNormalAveraging(TEXT("p.NormalAveraging"), NormalAveraging, TEXT(""));

int32 SampleMinParticlesForAcceleration = 2048;
FAutoConsoleVariableRef CVarSampleMinParticlesForAcceleration(TEXT("p.SampleMinParticlesForAcceleration"), SampleMinParticlesForAcceleration, TEXT("The minimum number of particles needed before using an acceleration structure when sampling"));

template <ECollisionUpdateType UpdateType, typename T, int d>
void SampleObject(const TImplicitObject<T, d>& Object, const TRigidTransform<T, d>& ObjectTransform, const TBVHParticles<T, d>& SampleParticles, const TRigidTransform<T, d>& SampleParticlesTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_SampleObject);
	TRigidBodyContactConstraint<T, d> AvgConstraint;
	AvgConstraint.ParticleIndex = Constraint.ParticleIndex;
	AvgConstraint.LevelsetIndex = Constraint.LevelsetIndex;
	AvgConstraint.Location = TVector<T, d>::ZeroVector;
	AvgConstraint.Normal = TVector<T, d>::ZeroVector;
	AvgConstraint.Phi = Thickness;
	T TotalThickness = T(0);

	int32 DeepestParticle = -1;
	const int32 NumParticles = SampleParticles.Size();

	const TRigidTransform<T, d> & SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
	if (NumParticles > SampleMinParticlesForAcceleration && Object.HasBoundingBox())
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetPartial);
		TBox<T, d> ImplicitBox = Object.BoundingBox().TransformedBox(ObjectTransform.GetRelativeTransform(SampleParticlesTransform));
		ImplicitBox.Thicken(Thickness);
		TArray<int32> PotentialParticles;
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetFindParticles);
			PotentialParticles = SampleParticles.FindAllIntersections(ImplicitBox);
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetSignedDistance);
			for (int32 i : PotentialParticles)
			{
				if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
				{
					SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
				}
				else
				{
					if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
					{
						DeepestParticle = i;
						if (UpdateType == ECollisionUpdateType::Any)
						{
							Constraint.Phi = AvgConstraint.Phi;
							return;
						}
					}
				}
			}
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetAll);
		for (int32 i = 0; i < NumParticles; ++i)
		{
			if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
			{
				const bool bInside = SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
			}
			else
			{
				if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
				{
					DeepestParticle = i;
					if (UpdateType == ECollisionUpdateType::Any)
					{
						Constraint.Phi = AvgConstraint.Phi;
						return;
					}
				}
			}
		}
	}

	if (NormalAveraging)
	{
		if (TotalThickness < -KINDA_SMALL_NUMBER)
		{
			TVector<T, d> LocalPoint = AvgConstraint.Location / TotalThickness;
			TVector<T, d> LocalNormal;
			const T NewPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
			if (NewPhi < Constraint.Phi)
			{
				Constraint.Phi = NewPhi;
				Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			}
		}
		else
		{
			check(AvgConstraint.Phi >= Thickness);
		}
	}
	else if(AvgConstraint.Phi < Constraint.Phi)
	{
		check(DeepestParticle >= 0);
		TVector<T,d> LocalPoint = SampleToObjectTM.TransformPositionNoScale(SampleParticles.X(DeepestParticle));
		TVector<T, d> LocalNormal;
		Constraint.Phi = Object.PhiWithNormal(LocalPoint, LocalNormal);
		Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
		Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			
	}
}

#if INTEL_ISPC
template<ECollisionUpdateType UpdateType>
void SampleObject(const TImplicitObject<float, 3>& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_SampleObject);
	TRigidBodyContactConstraint<float, 3> AvgConstraint;
	AvgConstraint.ParticleIndex = Constraint.ParticleIndex;
	AvgConstraint.LevelsetIndex = Constraint.LevelsetIndex;
	AvgConstraint.Location = TVector<float, 3>::ZeroVector;
	AvgConstraint.Normal = TVector<float, 3>::ZeroVector;
	AvgConstraint.Phi = Thickness;
	float TotalThickness = float(0);

	int32 DeepestParticle = -1;

	const TRigidTransform<float, 3>& SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
	int32 NumParticles = SampleParticles.Size();

	if (NumParticles > SampleMinParticlesForAcceleration && Object.HasBoundingBox())
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetPartial);
		TBox<float, 3> ImplicitBox = Object.BoundingBox().TransformedBox(ObjectTransform.GetRelativeTransform(SampleParticlesTransform));
		ImplicitBox.Thicken(Thickness);
		TArray<int32> PotentialParticles;
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetFindParticles);
			PotentialParticles = SampleParticles.FindAllIntersections(ImplicitBox);
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetSignedDistance);

			if (Object.GetType(true) == ImplicitObjectType::LevelSet && PotentialParticles.Num() > 0)
			{
				//QUICK_SCOPE_CYCLE_COUNTER(STAT_LevelSet);
				const TLevelSet<float, 3>* LevelSet = Object.GetObject<Chaos::TLevelSet<float, 3>>();
				const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

				if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
				{
					ispc::SampleLevelSetNormalAverage(
						(ispc::FVector&)Grid.MinCorner(),
						(ispc::FVector&)Grid.MaxCorner(),
						(ispc::FVector&)Grid.Dx(),
						(ispc::FIntVector&)Grid.Counts(),
						(ispc::TArrayND*)&LevelSet->GetPhiArray(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XView()[0],
						&PotentialParticles[0],
						Thickness,
						TotalThickness,
						(ispc::FVector&)AvgConstraint.Location,
						PotentialParticles.Num());
				}
				else
				{
					ispc::SampleLevelSetNoNormal(
						(ispc::FVector&)Grid.MinCorner(),
						(ispc::FVector&)Grid.MaxCorner(),
						(ispc::FVector&)Grid.Dx(),
						(ispc::FIntVector&)Grid.Counts(),
						(ispc::TArrayND*)&LevelSet->GetPhiArray(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XView()[0],
						&PotentialParticles[0],
						DeepestParticle,
						AvgConstraint.Phi,
						PotentialParticles.Num());

					if (UpdateType == ECollisionUpdateType::Any)
					{
						Constraint.Phi = AvgConstraint.Phi;
						return;
					}
				}
			}
			else if (Object.GetType(true) == ImplicitObjectType::Box && PotentialParticles.Num() > 0)
			{
				//QUICK_SCOPE_CYCLE_COUNTER(STAT_Box);
				const TBox<float, 3>* Box = Object.GetObject<Chaos::TBox<float, 3>>();

				if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
				{
					ispc::SampleBoxNormalAverage(
						(ispc::FVector&)Box->Min(),
						(ispc::FVector&)Box->Max(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XView()[0],
						&PotentialParticles[0],
						Thickness,
						TotalThickness,
						(ispc::FVector&)AvgConstraint.Location,
						PotentialParticles.Num());
				}
				else
				{
					ispc::SampleBoxNoNormal(
						(ispc::FVector&)Box->Min(),
						(ispc::FVector&)Box->Max(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XView()[0],
						&PotentialParticles[0],
						DeepestParticle,
						AvgConstraint.Phi,
						PotentialParticles.Num());

					if (UpdateType == ECollisionUpdateType::Any)
					{
						Constraint.Phi = AvgConstraint.Phi;
						return;
					}
				}
			}
			else
			{
				//QUICK_SCOPE_CYCLE_COUNTER(STAT_Other);
				for (int32 i : PotentialParticles)
				{
					if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
					{
						SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
					}
					else
					{
						if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
						{
							DeepestParticle = i;
							if (UpdateType == ECollisionUpdateType::Any)
							{
								Constraint.Phi = AvgConstraint.Phi;
								return;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetAll);
		if (Object.GetType(true) == ImplicitObjectType::LevelSet && NumParticles > 0)
		{
			const TLevelSet<float, 3>* LevelSet = Object.GetObject<Chaos::TLevelSet<float, 3>>();
			const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

			if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
			{
				ispc::SampleLevelSetNormalAverageAll(
					(ispc::FVector&)Grid.MinCorner(),
					(ispc::FVector&)Grid.MaxCorner(),
					(ispc::FVector&)Grid.Dx(),
					(ispc::FIntVector&)Grid.Counts(),
					(ispc::TArrayND*)&LevelSet->GetPhiArray(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XView()[0],
					Thickness,
					TotalThickness,
					(ispc::FVector&)AvgConstraint.Location,
					NumParticles);
			}
			else
			{
				ispc::SampleLevelSetNoNormalAll(
					(ispc::FVector&)Grid.MinCorner(),
					(ispc::FVector&)Grid.MaxCorner(),
					(ispc::FVector&)Grid.Dx(),
					(ispc::FIntVector&)Grid.Counts(),
					(ispc::TArrayND*)&LevelSet->GetPhiArray(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XView()[0],
					DeepestParticle,
					AvgConstraint.Phi,
					NumParticles);

				if (UpdateType == ECollisionUpdateType::Any)
				{
					Constraint.Phi = AvgConstraint.Phi;
					return;
				}
			}
		}
		else if (Object.GetType(true) == ImplicitObjectType::Plane && NumParticles > 0)
		{
			const TPlane<float, 3>* Plane = Object.GetObject<Chaos::TPlane<float, 3>>();

			if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
			{
				ispc::SamplePlaneNormalAverageAll(
					(ispc::FVector&)Plane->Normal(),
					(ispc::FVector&)Plane->X(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XView()[0],
					Thickness,
					TotalThickness,
					(ispc::FVector&)AvgConstraint.Location,
					NumParticles);
			}
			else
			{
				ispc::SamplePlaneNoNormalAll(
					(ispc::FVector&)Plane->Normal(),
					(ispc::FVector&)Plane->X(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XView()[0],
					DeepestParticle,
					AvgConstraint.Phi,
					NumParticles);

				if (UpdateType == ECollisionUpdateType::Any)
				{
					Constraint.Phi = AvgConstraint.Phi;
					return;
				}
			}
		}
		else if (Object.GetType(true) == ImplicitObjectType::Box && NumParticles > 0)
		{
			const TBox<float, 3>* Box = Object.GetObject<Chaos::TBox<float, 3>>();

			if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
			{
				ispc::SampleBoxNormalAverageAll(
					(ispc::FVector&)Box->Min(),
					(ispc::FVector&)Box->Max(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XView()[0],
					Thickness,
					TotalThickness,
					(ispc::FVector&)AvgConstraint.Location,
					NumParticles);
			}
			else
			{
				ispc::SampleBoxNoNormalAll(
					(ispc::FVector&)Box->Min(),
					(ispc::FVector&)Box->Max(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XView()[0],
					DeepestParticle,
					AvgConstraint.Phi,
					NumParticles);

				if (UpdateType == ECollisionUpdateType::Any)
				{
					Constraint.Phi = AvgConstraint.Phi;
					return;
				}
			}
		}
		else
		{
			//QUICK_SCOPE_CYCLE_COUNTER(STAT_Other);
			for (int32 i = 0; i < NumParticles; ++i)
			{
				if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
				{
					SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
				}
				else
				{
					if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
					{
						DeepestParticle = i;
						if (UpdateType == ECollisionUpdateType::Any)
						{
							Constraint.Phi = AvgConstraint.Phi;
							return;
						}
					}
				}
			}
		}
	}

	if (NormalAveraging)
	{
		if (TotalThickness < -KINDA_SMALL_NUMBER)
		{
			TVector<float, 3> LocalPoint = AvgConstraint.Location / TotalThickness;
			TVector<float, 3> LocalNormal;
			const float NewPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
			if (NewPhi < Constraint.Phi)
			{
				Constraint.Phi = NewPhi;
				Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			}
		}
		else
		{
			check(AvgConstraint.Phi >= Thickness);
		}
	}
	else if (AvgConstraint.Phi < Constraint.Phi)
	{
		check(DeepestParticle >= 0);
		TVector<float, 3> LocalPoint = SampleToObjectTM.TransformPositionNoScale(SampleParticles.X(DeepestParticle));
		TVector<float, 3> LocalNormal;
		Constraint.Phi = Object.PhiWithNormal(LocalPoint, LocalNormal);
		Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
		Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
	}
}
#endif

template <typename T, int d>
bool UpdateBoxPlaneConstraint(const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	
#if USING_CODE_ANALYSIS
	MSVC_PRAGMA( warning( push ) )
	MSVC_PRAGMA( warning( disable : ALL_CODE_ANALYSIS_WARNINGS ) )
#endif	// USING_CODE_ANALYSIS

	bool bApplied = false;
	const TRigidTransform<T, d> BoxToPlaneTransform(BoxTransform.GetRelativeTransform(PlaneTransform));
	const TVector<T, d> Extents = Box.Extents();
	constexpr int32 NumCorners = 2 + 2 * d;
	constexpr T Epsilon = KINDA_SMALL_NUMBER;

	TVector<T, d> Corners[NumCorners];
	int32 CornerIdx = 0;
	Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max());
	Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min());
	for (int32 j = 0; j < d; ++j)
	{
		Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min() + TVector<T, d>::AxisVector(j) * Extents);
		Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max() - TVector<T, d>::AxisVector(j) * Extents);
	}

#if USING_CODE_ANALYSIS
	MSVC_PRAGMA( warning( pop ) )
#endif	// USING_CODE_ANALYSIS

	TVector<T, d> PotentialConstraints[NumCorners];
	int32 NumConstraints = 0;
	for (int32 i = 0; i < NumCorners; ++i)
	{
		TVector<T, d> Normal;
		const T NewPhi = Plane.PhiWithNormal(Corners[i], Normal);
		if (NewPhi < Constraint.Phi + Epsilon)
		{
			if (NewPhi <= Constraint.Phi - Epsilon)
			{
				NumConstraints = 0;
			}
			Constraint.Phi = NewPhi;
			Constraint.Normal = PlaneTransform.TransformVector(Normal);
			Constraint.Location = PlaneTransform.TransformPosition(Corners[i]);
			PotentialConstraints[NumConstraints++] = Constraint.Location;
			bApplied = true;
		}
	}
	if (NumConstraints > 1)
	{
		TVector<T, d> AverageLocation(0);
		for (int32 ConstraintIdx = 0; ConstraintIdx < NumConstraints; ++ConstraintIdx)
		{
			AverageLocation += PotentialConstraints[ConstraintIdx];
		}
		Constraint.Location = AverageLocation / NumConstraints;
	}

	return bApplied;
}

template <typename T, int d>
void UpdateSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	const TVector<T, d> Center1 = Sphere1Transform.TransformPosition(Sphere1.Center());
	const TVector<T, d> Center2 = Sphere2Transform.TransformPosition(Sphere2.Center());
	const TVector<T, d> Direction = Center1 - Center2;
	const T Size = Direction.Size();
	const T NewPhi = Size - (Sphere1.Radius() + Sphere2.Radius());
	if (NewPhi < Constraint.Phi)
	{
		Constraint.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
		Constraint.Phi = NewPhi;
		Constraint.Location = Center1 - Sphere1.Radius() * Constraint.Normal;
	}
}

template <typename T, int d>
void UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	const TRigidTransform<T, d> SphereToPlaneTransform(PlaneTransform.Inverse() * SphereTransform);
	const TVector<T, d> SphereCenter = SphereToPlaneTransform.TransformPosition(Sphere.Center());

	TVector<T, d> NewNormal;
	T NewPhi = Plane.PhiWithNormal(SphereCenter, NewNormal);
	NewPhi -= Sphere.Radius();

	if (NewPhi < Constraint.Phi)
	{
		Constraint.Phi = NewPhi;
		Constraint.Normal = PlaneTransform.TransformVectorNoScale(NewNormal);
		Constraint.Location = SphereCenter - Constraint.Normal * Sphere.Radius();
	}
}

template <typename T, int d>
bool UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	const TRigidTransform<T, d> SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());
	const TVector<T, d> SphereCenterInBox = SphereToBoxTransform.TransformPosition(Sphere.Center());

	TVector<T, d> NewNormal;
	T NewPhi = Box.PhiWithNormal(SphereCenterInBox, NewNormal);
	NewPhi -= Sphere.Radius();

	if (NewPhi < Constraint.Phi)
	{
		Constraint.Phi = NewPhi;
		Constraint.Normal = BoxTransform.TransformVectorNoScale(NewNormal);
		Constraint.Location = SphereTransform.TransformPosition(Sphere.Center()) - Constraint.Normal * Sphere.Radius();
		return true;
	}

	return false;
}

DECLARE_CYCLE_STAT(TEXT("FindRelevantShapes"), STAT_FindRelevantShapes, STATGROUP_ChaosWide);
template <typename T, int d>
TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> FindRelevantShapes(const TImplicitObject<T,d>* ParticleObj, const TRigidTransform<T,d>& ParticlesTM, const TImplicitObject<T,d>& LevelsetObj, const TRigidTransform<T,d>& LevelsetTM, const T Thickness)
{
	SCOPE_CYCLE_COUNTER(STAT_FindRelevantShapes);
	TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> RelevantShapes;
	//find all levelset inner objects
	if (ParticleObj)
	{
		if (ParticleObj->HasBoundingBox())
		{
			const TRigidTransform<T, d> ParticlesToLevelsetTM = ParticlesTM.GetRelativeTransform(LevelsetTM);
			TBox<T, d> ParticleBoundsInLevelset = ParticleObj->BoundingBox().TransformedBox(ParticlesToLevelsetTM);
			ParticleBoundsInLevelset.Thicken(Thickness);
			{
				LevelsetObj.FindAllIntersectingObjects(RelevantShapes, ParticleBoundsInLevelset);
			}
		}
		else
		{
			LevelsetObj.AccumulateAllImplicitObjects(RelevantShapes, TRigidTransform<T, d>::Identity);
		}
	}
	else
	{
		//todo:compute bounds
		LevelsetObj.AccumulateAllImplicitObjects(RelevantShapes, TRigidTransform<T, d>::Identity);
	}

	return RelevantShapes;
}

DECLARE_CYCLE_STAT(TEXT("UpdateUnionUnionConstraint"), STAT_UpdateUnionUnionConstraint, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, class T_PARTICLES, typename T, int d>
void UpdateUnionUnionConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateUnionUnionConstraint);
	Constraint.Phi = Thickness;

	TRigidTransform<T, d> ParticlesTM = GetTransform(InParticles, Constraint.ParticleIndex);
	TRigidTransform<T, d> LevelsetTM = GetTransform(InParticles, Constraint.LevelsetIndex);

	const TImplicitObject<T, d>* ParticleObj = InParticles.Geometry(Constraint.ParticleIndex).Get();
	const TImplicitObject<T,d>* LevelsetObj = InParticles.Geometry(Constraint.LevelsetIndex).Get();
	const TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

	for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
	{
		const TImplicitObject<T, d>& LevelsetInnerObj = *LevelsetObjPair.First;
		const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * LevelsetTM;

		//now find all particle inner objects
		const TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(&LevelsetInnerObj, LevelsetInnerObjTM, *ParticleObj, ParticlesTM, Thickness);

		//for each inner obj pair, update constraint
		for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
		{
			const TImplicitObject<T, d>& ParticleInnerObj = *ParticlePair.First;
			const TRigidTransform<T, d> ParticleInnerObjTM = ParticlePair.Second * ParticlesTM;
			UpdateConstraintImp<UpdateType>(InParticles, ParticleInnerObj, ParticleInnerObjTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateSingleUnionConstraint"), STAT_UpdateSingleUnionConstraint, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, class T_PARTICLES, typename T, int d>
void UpdateSingleUnionConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateSingleUnionConstraint);
	Constraint.Phi = Thickness;

	TRigidTransform<T, d> ParticlesTM = GetTransform(InParticles, Constraint.ParticleIndex);
	TRigidTransform<T, d> LevelsetTM = GetTransform(InParticles, Constraint.LevelsetIndex);

	const TImplicitObject<T, d>* ParticleObj = InParticles.Geometry(Constraint.ParticleIndex).Get();
	const TImplicitObject<T, d>* LevelsetObj = InParticles.Geometry(Constraint.LevelsetIndex).Get();
	const TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);
	
	for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
	{
		const TImplicitObject<T, d>& LevelsetInnerObj = *LevelsetObjPair.First;
		const TRigidTransform<T, d> LevelsetInnerObjTM = LevelsetTM * LevelsetObjPair.Second;
		UpdateConstraintImp<UpdateType>(InParticles, *ParticleObj, ParticlesTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetConstraint"), STAT_UpdateLevelsetConstraint, STATGROUP_ChaosWide);
template<typename T, int d>
template<ECollisionUpdateType UpdateType, class T_PARTICLES>
void TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetConstraint);
	Constraint.Phi = Thickness;

	TRigidTransform<T, d> ParticlesTM = GetTransform(InParticles, Constraint.ParticleIndex);
	if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
	{
		return;
	}
	TRigidTransform<T, d> LevelsetTM = GetTransform(InParticles, Constraint.LevelsetIndex);
	if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
	{
		return;
	}

	const TBVHParticles<T, d>* SampleParticles = nullptr;
	SampleParticles = InParticles.CollisionParticles(Constraint.ParticleIndex).Get();

	if(SampleParticles)
	{
		SampleObject<UpdateType>(*InParticles.Geometry(Constraint.LevelsetIndex), LevelsetTM, *SampleParticles, ParticlesTM, Thickness, Constraint);
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateUnionLevelsetConstraint"), STAT_UpdateUnionLevelsetConstraint, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, class T_PARTICLES, typename T, int d>
void UpdateUnionLevelsetConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateUnionLevelsetConstraint);
	Constraint.Phi = Thickness;

	TRigidTransform<T, d> ParticlesTM = GetTransform(InParticles, Constraint.ParticleIndex);
	TRigidTransform<T, d> LevelsetTM = GetTransform(InParticles, Constraint.LevelsetIndex);

	if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
	{
		return;
	}

	if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
	{
		return;
	}

	const TImplicitObject<T, d>* ParticleObj = InParticles.Geometry(Constraint.ParticleIndex).Get();
	const TImplicitObject<T, d>* LevelsetObj = InParticles.Geometry(Constraint.LevelsetIndex).Get();
	TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

	if (LevelsetShapes.Num() && InParticles.CollisionParticles(Constraint.ParticleIndex).Get())
	{
		const TBVHParticles<T, d>& SampleParticles = *InParticles.CollisionParticles(Constraint.ParticleIndex).Get();
		if (SampleParticles.Size())
		{
			for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const TImplicitObject<T, d>* Object = LevelsetObjPair.First;
				const TRigidTransform<T, d> ObjectTM = LevelsetObjPair.Second * LevelsetTM;
				SampleObject<UpdateType>(*Object, ObjectTM, SampleParticles, ParticlesTM, Thickness, Constraint);
				if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
				{
					return;
				}
			}
		}
		else if (ParticleObj && ParticleObj->IsUnderlyingUnion())
		{
			const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
			//need to traverse shapes to get their collision particles
			for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const TImplicitObject<T, d>* LevelsetInnerObject = LevelsetObjPair.First;
				const TRigidTransform<T, d> LevelsetInnerObjectTM = LevelsetObjPair.Second * LevelsetTM;

				TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(LevelsetInnerObject, LevelsetInnerObjectTM, *ParticleObj, ParticlesTM, Thickness);
				for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
				{
					const TImplicitObject<T, d>* ParticleInnerObject = ParticleObjPair.First;
					const TRigidTransform<T, d> ParticleInnerObjectTM = ParticleObjPair.Second * ParticlesTM;

					if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(ParticleInnerObject))
					{
						const TBVHParticles<T, d>& InnerSampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
						SampleObject<UpdateType>(*LevelsetInnerObject, LevelsetInnerObjectTM, InnerSampleParticles, ParticleInnerObjectTM, Thickness, Constraint);
						if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
						{
							return;
						}
					}
				}

			}
		}
	}
}


DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetUnionConstraint"), STAT_UpdateLevelsetUnionConstraint, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, class T_PARTICLES, typename T, int d>
void UpdateLevelsetUnionConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetUnionConstraint);
	Constraint.Phi = Thickness;

	TRigidTransform<T, d> ParticlesTM = GetTransform(InParticles, Constraint.ParticleIndex);
	TRigidTransform<T, d> LevelsetTM = GetTransform(InParticles, Constraint.LevelsetIndex);

	if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
	{
		return;
	}

	if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
	{
		return;
	}

	const TImplicitObject<T, d>* ParticleObj = InParticles.Geometry(Constraint.ParticleIndex).Get();
	const TImplicitObject<T, d>* LevelsetObj = InParticles.Geometry(Constraint.LevelsetIndex).Get();

	TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(LevelsetObj, LevelsetTM, *ParticleObj, ParticlesTM, Thickness);
	check(ParticleObj->IsUnderlyingUnion());
	const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
	for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
	{
		const TImplicitObject<T, d>* Object = ParticleObjPair.First;

		if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(Object))
		{
			const TBVHParticles<T, d>& SampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
			const TRigidTransform<T, d> ObjectTM = ParticleObjPair.Second * ParticlesTM;

			SampleObject<UpdateType>(*LevelsetObj, LevelsetTM, SampleParticles, ObjectTM, Thickness, Constraint);
			if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
			{
				return;
			}
		}
	}
}

template<typename T, int d>
template<ECollisionUpdateType UpdateType, class T_PARTICLES>
void TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraintGJK(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	static int32 MaxIterations = 100;
	Constraint.Phi = Thickness;
	const TRigidTransform<T, d> LocalToWorld1 = GetTransform(InParticles, Constraint.ParticleIndex);
	const TRigidTransform<T, d> LocalToWorld2 = GetTransform(InParticles, Constraint.LevelsetIndex);
	TVector<T, d> Direction = LocalToWorld1.GetTranslation() - LocalToWorld2.GetTranslation();
	TVector<T, d> SupportA = LocalToWorld1.TransformPosition(InParticles.Geometry(Constraint.ParticleIndex)->Support(LocalToWorld1.InverseTransformVector(-Direction), Thickness));
	TVector<T, d> SupportB = LocalToWorld2.TransformPosition(InParticles.Geometry(Constraint.LevelsetIndex)->Support(LocalToWorld2.InverseTransformVector(Direction), Thickness));
	TVector<T, d> Point = SupportB - SupportA;
	TArray<Pair<TVector<T, d>, TVector<T, d>>> Points = {MakePair(Point, SupportA)};
	Direction = -Point;
	for (int32 i = 0; i < MaxIterations; ++i)
	{
		SupportA = LocalToWorld1.TransformPosition(InParticles.Geometry(Constraint.ParticleIndex)->Support(LocalToWorld1.InverseTransformVector(-Direction), Thickness));
		SupportB = LocalToWorld2.TransformPosition(InParticles.Geometry(Constraint.LevelsetIndex)->Support(LocalToWorld2.InverseTransformVector(Direction), Thickness));
		Point = SupportB - SupportA;
		if (TVector<T, d>::DotProduct(Point, Direction) < 0)
		{
			break;
		}
		Points.Add(MakePair(Point, SupportA));
		TVector<T, d> ClosestPoint;
		if (NearestPoint(Points, Direction, ClosestPoint))
		{
			TVector<T, d> Body1Location = LocalToWorld1.InverseTransformPosition(ClosestPoint);
			TVector<T, d> Normal;
			T Phi = InParticles.Geometry(Constraint.ParticleIndex)->PhiWithNormal(Body1Location, Normal);
			Normal = LocalToWorld1.TransformVector(Normal);
			Constraint.Location = ClosestPoint - Phi * Normal;
			TVector<T, d> Body2Location = LocalToWorld2.InverseTransformPosition(Constraint.Location);
			Constraint.Phi = InParticles.Geometry(Constraint.LevelsetIndex)->PhiWithNormal(Body2Location, Constraint.Normal);
			Constraint.Normal = LocalToWorld2.TransformVector(Constraint.Normal);
			break;
		}
	}
}

template <typename T, int d>
void UpdateBoxConstraint(const TBox<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TBox<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TBox<T,d> Box2SpaceBox1 = Box1.TransformedBox(Box1Transform * Box2Transform.Inverse());
	TBox<T,d> Box1SpaceBox2 = Box2.TransformedBox(Box2Transform * Box1Transform.Inverse());
	Box2SpaceBox1.Thicken(Thickness);
	Box1SpaceBox2.Thicken(Thickness);
	if (Box1SpaceBox2.Intersects(Box1) && Box2SpaceBox1.Intersects(Box2))
	{
		const TVector<T, d> Box1Center = (Box1Transform * Box2Transform.Inverse()).TransformPosition(Box1.Center());
		bool bDeepOverlap = false;
		if (Box2.SignedDistance(Box1Center) < 0)
		{
			//If Box1 is overlapping Box2 by this much the signed distance approach will fail (box1 gets sucked into box2). In this case just use two spheres
			TSphere<T, d> Sphere1(Box1Transform.TransformPosition(Box1.Center()), Box1.Extents().Min() / 2);
			TSphere<T, d> Sphere2(Box2Transform.TransformPosition(Box2.Center()), Box2.Extents().Min() / 2);
			const TVector<T, d> Direction = Sphere1.Center() - Sphere2.Center();
			T Size = Direction.Size();
			if (Size < (Sphere1.Radius() + Sphere2.Radius()))
			{
				const T NewPhi = Size - (Sphere1.Radius() + Sphere2.Radius());;
				if (NewPhi < Constraint.Phi)
				{
					bDeepOverlap = true;
					Constraint.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
					Constraint.Phi = NewPhi;
					Constraint.Location = Sphere1.Center() - Sphere1.Radius() * Constraint.Normal;
				}
			}
		}
		if (!bDeepOverlap || Constraint.Phi >= 0)
		{
			//if we didn't have deep penetration use signed distance per particle. If we did have deep penetration but the spheres did not overlap use signed distance per particle

			//UpdateLevelsetConstraintGJK(InParticles, Thickness, Constraint);
			//check(Constraint.Phi < MThickness);
			// For now revert to doing all points vs lsv check until we can figure out a good way to get the deepest point without needing this
			{
				const TArray<TVector<T, d>> SampleParticles = Box1.ComputeLocalSamplePoints();
				const TRigidTransform<T, d> Box1ToBox2Transform = Box1Transform.GetRelativeTransform(Box2Transform);
				int32 NumParticles = SampleParticles.Num();
				for (int32 i = 0; i < NumParticles; ++i)
				{
					SampleObjectHelper(Box2, Box2Transform, Box1ToBox2Transform, SampleParticles[i], Thickness, Constraint);
				}
			}
		}
	}
}


template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeLevelsetConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const T Thickness)
{
	if (!InParticles.Geometry(LevelsetIndex) || (!InParticles.CollisionParticlesSize(ParticleIndex) && InParticles.Geometry(ParticleIndex) && !InParticles.Geometry(ParticleIndex)->IsUnderlyingUnion()))
	{
		int32 TmpIndex = ParticleIndex;
		ParticleIndex = LevelsetIndex;
		LevelsetIndex = TmpIndex;
	}
	check(InParticles.Geometry(LevelsetIndex));
	//todo(ocohen):if both have collision particles, use the one with fewer?
	// Find Deepest Point
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = ParticleIndex;
	Constraint.LevelsetIndex = LevelsetIndex;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeLevelsetConstraintGJK(const TPBDRigidParticles<T, d>& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = ParticleIndex;
	Constraint.LevelsetIndex = LevelsetIndex;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeBoxConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Box1Index, int32 Box2Index, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = Box1Index;
	Constraint.LevelsetIndex = Box2Index;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeBoxPlaneConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 BoxIndex, int32 PlaneIndex, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = BoxIndex;
	Constraint.LevelsetIndex = PlaneIndex;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSphereConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Sphere1Index, int32 Sphere2Index, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = Sphere1Index;
	Constraint.LevelsetIndex = Sphere2Index;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSpherePlaneConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 SphereIndex, int32 PlaneIndex, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = SphereIndex;
	Constraint.LevelsetIndex = PlaneIndex;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSphereBoxConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 SphereIndex, int32 BoxIndex, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = SphereIndex;
	Constraint.LevelsetIndex = BoxIndex;
	return Constraint;
}

template <typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSingleUnionConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 NonUnionIndex, int32 UnionIndex, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = NonUnionIndex;
	Constraint.LevelsetIndex = UnionIndex;
	return Constraint;
}

template <typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeUnionUnionConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Union1Index, int32 Union2Index, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.ParticleIndex = Union1Index;
	Constraint.LevelsetIndex = Union2Index;
	//todo(ocohen): some heuristic for determining the order?
	return Constraint;
}

template<typename T, int d>
typename TPBDCollisionConstraint<T, d>::FRigidBodyContactConstraint TPBDCollisionConstraint<T, d>::ComputeConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Body1Index, int32 Body2Index, const T Thickness)
{
	if (!InParticles.Geometry(Body1Index) || !InParticles.Geometry(Body2Index))
	{
		return ComputeLevelsetConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	if (InParticles.Geometry(Body1Index)->GetType() == TBox<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TBox<T, d>::GetType())
	{
		return ComputeBoxConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TSphere<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TSphere<T, d>::GetType())
	{
		return ComputeSphereConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TBox<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TPlane<T, d>::GetType())
	{
		return ComputeBoxPlaneConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body2Index)->GetType() == TPlane<T, d>::GetType() && InParticles.Geometry(Body1Index)->GetType() == TBox<T, d>::GetType())
	{
		return ComputeBoxPlaneConstraint(InParticles, Body2Index, Body1Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TSphere<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TPlane<T, d>::GetType())
	{
		return ComputeSpherePlaneConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body2Index)->GetType() == TPlane<T, d>::GetType() && InParticles.Geometry(Body1Index)->GetType() == TSphere<T, d>::GetType())
	{
		return ComputeSpherePlaneConstraint(InParticles, Body2Index, Body1Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TSphere<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TBox<T, d>::GetType())
	{
		return ComputeSphereBoxConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() < TImplicitObjectUnion<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return ComputeSingleUnionConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TImplicitObjectUnion<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() < TImplicitObjectUnion<T, d>::GetType())
	{
		return ComputeSingleUnionConstraint(InParticles, Body2Index, Body1Index, Thickness);
	}
	else if(InParticles.Geometry(Body1Index)->GetType() == TImplicitObjectUnion<T, d>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return ComputeUnionUnionConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
#if 0
	else if (InParticles.Geometry(Body1Index)->IsConvex() && InParticles.Geometry(Body2Index)->IsConvex())
	{
		return ComputeLevelsetConstraintGJK(InParticles, Body1Index, Body2Index, Thickness);
	}
#endif
	return ComputeLevelsetConstraint(InParticles, Body1Index, Body2Index, Thickness);
}

// NOTE: UpdateLevelsetConstraintImp and its <float,3> specialization are here for the Linux build. It looks like
// GCC does not resolve TPBDCollisionConstraint<T, d> correctly and it won't compile without them.
template<ECollisionUpdateType UpdateType, class T_PARTICLES, typename T, int d>
void UpdateLevelsetConstraintImp(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraint<UpdateType>(InParticles, Thickness, Constraint);
}

template<ECollisionUpdateType UpdateType>
void UpdateLevelsetConstraintImp(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint)
{
	TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<UpdateType>(InParticles, Thickness, Constraint);
}

template<ECollisionUpdateType UpdateType, class T_PARTICLES, typename T, int d>
void UpdateConstraintImp(const T_PARTICLES& InParticles, const TImplicitObject<T, d>& ParticleObject, const TRigidTransform<T, d>& ParticleTM, const TImplicitObject<T, d>& LevelsetObject, const TRigidTransform<T, d>& LevelsetTM, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		UpdateBoxConstraint(*ParticleObject.template GetObject<TBox<T,d>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TSphere<T, d>::GetType() && LevelsetObject.GetType() == TSphere<T, d>::GetType())
	{
		UpdateSphereConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TPlane<T, d>::GetType())
	{
		UpdateBoxPlaneConstraint(*ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TPlane<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TSphere<T, d>::GetType() && LevelsetObject.GetType() == TPlane<T, d>::GetType())
	{
		UpdateSpherePlaneConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TPlane<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TSphere<T, d>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		UpdateSphereBoxConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TPlane<T, d>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateBoxPlaneConstraint(*LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TPlane<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() == TPlane<T, d>::GetType() && LevelsetObject.GetType() == TSphere<T, d>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateSpherePlaneConstraint(*LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TPlane<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TSphere<T, d>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateSphereBoxConstraint(*LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() < TImplicitObjectUnion<T, d>::GetType() && LevelsetObject.GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return UpdateSingleUnionConstraint<UpdateType>(InParticles, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TImplicitObjectUnion<T, d>::GetType() && LevelsetObject.GetType() < TImplicitObjectUnion<T, d>::GetType())
	{
		check(false);	//should not be possible to get this ordering (see ComputeConstraint)
	}
	else if (ParticleObject.GetType() == TImplicitObjectUnion<T, d>::GetType() && LevelsetObject.GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return UpdateUnionUnionConstraint<UpdateType>(InParticles, Thickness, Constraint);
	}
#if 0
	else if (ParticleObject.IsConvex() && LevelsetObject.IsConvex())
	{
		UpdateLevelsetConstraintGJK<UpdateType>(InParticles, Thickness, Constraint);
	}
#endif
	else if (LevelsetObject.IsUnderlyingUnion())
	{
		UpdateUnionLevelsetConstraint<UpdateType>(InParticles, Thickness, Constraint);
	}
	else if (ParticleObject.IsUnderlyingUnion())
	{
		UpdateLevelsetUnionConstraint<UpdateType>(InParticles, Thickness, Constraint);
	}
	else
	{
		UpdateLevelsetConstraintImp<UpdateType>(InParticles, Thickness, Constraint);
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateConstraint"), STAT_UpdateConstraint, STATGROUP_ChaosWide);

template<typename T, int d>
template<ECollisionUpdateType UpdateType, class T_PARTICLES>
void TPBDCollisionConstraint<T, d>::UpdateConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateConstraint);
	Constraint.Phi = Thickness;
	const TRigidTransform<T, d> ParticleTM = GetTransform(InParticles, Constraint.ParticleIndex);
	const TRigidTransform<T, d> LevelsetTM = GetTransform(InParticles, Constraint.LevelsetIndex);

	if (!InParticles.Geometry(Constraint.ParticleIndex))
	{
		if (InParticles.Geometry(Constraint.LevelsetIndex))
		{
			if (!InParticles.Geometry(Constraint.LevelsetIndex)->IsUnderlyingUnion())
			{
				TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraint<UpdateType>(InParticles, Thickness, Constraint);
			}
			else
			{
				UpdateUnionLevelsetConstraint<UpdateType>(InParticles, Thickness, Constraint);
			}
		}
	}
	else
	{
		UpdateConstraintImp<UpdateType>(InParticles, *InParticles.Geometry(Constraint.ParticleIndex), ParticleTM, *InParticles.Geometry(Constraint.LevelsetIndex), LevelsetTM, Thickness, Constraint);
	}
}

template class TPBDCollisionConstraint<float, 3>;
template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any, TPBDRigidParticles<float, 3>>(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest, TPBDRigidParticles<float, 3>>(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Any, TPBDRigidParticles<float, 3>>(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Deepest, TPBDRigidParticles<float, 3>>(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraintGJK<ECollisionUpdateType::Any, TPBDRigidParticles<float, 3>>(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraintGJK<ECollisionUpdateType::Deepest, TPBDRigidParticles<float, 3>>(const TPBDRigidParticles<float, 3>& InParticles, const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(const TPBDRigidParticles<float, 3>& InParticles, const TArray<int32>& InIndices, float Dt);
template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(const TPBDRigidParticles<float, 3>& InParticles, const TArray<int32>& InIndices, float Dt);

template void UpdateConstraintImp<ECollisionUpdateType::Any, TPBDRigidParticles<float, 3>, float, 3>(const TPBDRigidParticles<float, 3>& InParticles, const TImplicitObject<float, 3>& ParticleObject, const TRigidTransform<float, 3>& ParticleTM, const TImplicitObject<float, 3>& LevelsetObject, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
template void UpdateConstraintImp<ECollisionUpdateType::Deepest, TPBDRigidParticles<float, 3>, float, 3>(const TPBDRigidParticles<float, 3>& InParticles, const TImplicitObject<float, 3>& ParticleObject, const TRigidTransform<float, 3>& ParticleTM, const TImplicitObject<float, 3>& LevelsetObject, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
}
