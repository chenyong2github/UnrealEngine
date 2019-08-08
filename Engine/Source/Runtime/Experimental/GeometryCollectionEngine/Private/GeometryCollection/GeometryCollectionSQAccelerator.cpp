// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionSQAccelerator.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/Sphere.h"
#include "Chaos/PBDRigidParticles.h"
#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "Components/BoxComponent.h"
#include "ChaosSolversModule.h"
#include "ChaosStats.h"
#include "PhysicsSolver.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"

#if INCLUDE_CHAOS && !WITH_CHAOS_NEEDS_TO_BE_FIXED


DECLARE_CYCLE_STAT(TEXT("LowLevelSweep"), STAT_LowLevelSweep, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("LowLevelRaycast"), STAT_LowLevelRaycast, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("LowLevelOverlap"), STAT_LowLevelOverlap, STATGROUP_Chaos);

#if TODO_REIMPLEMENT_SCENEQUERY_CROSSENGINE

bool IsValidIndexAndTransform(const FGeometryCollectionResults& PhysResult, const Chaos::TPBDRigidParticles<float, 3>& Particles, const TManagedArray<FTransform>& TransformArray, const TArray<bool>& DisabledFlags, const int32 RigidBodyIdx, const bool bCanBeDisabled)
{
	if(RigidBodyIdx == -1)
	{
		//todo(ocohen): managed to avoid this invalid index, but need to investigate a bit more into whether we can always assume it's valid
		return false;
	}

	if (PhysResult.BaseIndex == -1)
	{
		//todo(mlentine): Why is this possible?
		return false;
	}

	const int32 LocalBodyIndex = RigidBodyIdx - PhysResult.BaseIndex;

	if(LocalBodyIndex < 0 || LocalBodyIndex >= PhysResult.NumParticlesAdded)
	{
		// Ignore collisions for other components - need to make this even faster (subset of potential intersections [maybe array view?])
		return false;
	}

	const FTransform& CurrentTransform = TransformArray[LocalBodyIndex];
	const FVector CurrentTranslation = CurrentTransform.GetTranslation();

	//@todo(mlentine): In theory this is no longer needed
	if (!bCanBeDisabled && DisabledFlags[LocalBodyIndex]) 
	{ 
		//disabled particles can actually have stale geometry in them and are clearly not useful anyway
		return false;
	}

	if (static_cast<uint32>(RigidBodyIdx) >= Particles.Size())
	{
		// @todo(mlentine): Is this a possible situation?
		return false;
	}

	if (!(ensure(!FMath::IsNaN(CurrentTranslation[0])) && ensure(!FMath::IsNaN(CurrentTranslation[1])) && ensure(!FMath::IsNaN(CurrentTranslation[2]))))
	{
		return false;
	}

	return true;
}

bool LowLevelRaycastSingleElement(int32 InParticleIndex, const Chaos::FPhysicsSolver* InSolver, const Chaos::TClusterBuffer<float, 3>& ClusterBuffer, const FGeometryCollectionPhysicsProxy* InObject, const FVector& Start, const FVector& Dir, float DeltaMag, bool bCanBeDisabled, EHitFlags OutputFlags, FHitRaycast& OutHit)
{
	using namespace Chaos;

	// Preconditions from Raycast - shouldn't get in here without valid case
	checkSlow(InSolver);
	checkSlow(InObject);

	const FGeometryCollectionResults& PhysResult = InObject->GetPhysicsResults().GetGameDataForRead();

	const TManagedArray<int32>& RigidBodyIdArray = PhysResult.RigidBodyIds;
	const TManagedArray<FTransform>& TransformArray = PhysResult.Transforms;
	const TArray<bool>& DisabledFlags = PhysResult.DisabledStates;

	{
		const TPBDRigidParticles<float, 3>& Particles = InSolver->GetRigidParticles();

		if(!IsValidIndexAndTransform(PhysResult, Particles, TransformArray, DisabledFlags, InParticleIndex, bCanBeDisabled))
		{
			return false;
		}

		const int32 LocalBodyIndex = InParticleIndex - PhysResult.BaseIndex;

		const TRigidTransform<float, 3>& TM = PhysResult.ParticleToWorldTransforms[LocalBodyIndex];
		if (!(ensure(!FMath::IsNaN(TM.GetTranslation().X)) && ensure(!FMath::IsNaN(TM.GetTranslation().Y)) && ensure(!FMath::IsNaN(TM.GetTranslation().Z))))
		{
			return false;
		}

		const TVector<float, 3> StartLocal = TM.InverseTransformPositionNoScale(Start);
		const TVector<float, 3> DirLocal = TM.InverseTransformVectorNoScale(Dir);
		const TVector<float, 3> EndLocal = StartLocal + DirLocal * DeltaMag;

		const TImplicitObject<float, 3>* Object = ClusterBuffer.GeometryPtrs[InParticleIndex].Get();	//todo(ocohen): can this ever be null?

		if (!Object)
		{
			return false;
		}
		Pair<TVector<float, 3>, bool> Result = Object->FindClosestIntersection(StartLocal, EndLocal, /*Thickness=*/0.f);
		if(Result.Second)	//todo(ocohen): once we do more than just a bool we need to get the closest point
		{
#if WITH_PHYSX
			//todo(ocohen): check output flags?
			const float Distance = (Result.First - StartLocal).Size();
			if(OutHit.distance == PX_MAX_REAL || Distance < OutHit.distance)
			{
				OutHit.distance = Distance;	//todo(ocohen): assuming physx structs for now
				OutHit.position = U2PVector(TM.TransformPositionNoScale(Result.First));
				const TVector<float, 3> LocalNormal = Object->Normal(Result.First);
				OutHit.normal = U2PVector(TM.TransformVectorNoScale(LocalNormal));
				SetFlags(OutHit, EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position);
			}
			return true;
#endif
		}
	}

	return false;
}

static TAutoConsoleVariable<int32> CVarMaxSweepSteps(
	TEXT("p.MaxSweepSteps"),
	3,
	TEXT("Number of steps during a sweep"),
	ECVF_Default);

bool LowLevelSweepSingleElement(int32 InParticleIndex, const Chaos::FPhysicsSolver* InSolver, const Chaos::TClusterBuffer<float, 3>& ClusterBuffer, const FGeometryCollectionPhysicsProxy* InObject, const Chaos::TImplicitObject<float, 3>& QueryGeom, const Chaos::TParticles<float, 3>& CollisionParticles, const FTransform& StartPose, const FVector& Dir, float DeltaMag, const bool bCanBeDisabled, FHitSweep& OutHit)
{
	using namespace Chaos;

	checkSlow(InSolver);
	checkSlow(InObject);

	const FGeometryCollectionResults& PhysResult = InObject->GetPhysicsResults().GetGameDataForRead();

	const TManagedArray<int32>& RigidBodyIdArray = PhysResult.RigidBodyIds;
	const TManagedArray<FTransform>& TransformArray = PhysResult.Transforms;
	const TArray<bool>& DisabledFlags = PhysResult.DisabledStates;

	const TPBDRigidParticles<float, 3>& Particles = InSolver->GetRigidParticles();

	if(!IsValidIndexAndTransform(PhysResult, Particles, TransformArray, DisabledFlags, InParticleIndex, bCanBeDisabled))
	{
		return false;
	}

	const int32 LocalBodyIndex = InParticleIndex - PhysResult.BaseIndex;
	const TRigidTransform<float, 3>& TM = PhysResult.ParticleToWorldTransforms[LocalBodyIndex];

	const TImplicitObject<float, 3>* Object = ClusterBuffer.GeometryPtrs[InParticleIndex].Get();
	if (!Object)
	{
		return false;
	}
	const TVector<float, 3> DirLocal = TM.InverseTransformVectorNoScale(Dir);

	bool bFound = false;

	for(uint32 i = 0; i < CollisionParticles.Size(); ++i)
	{
		const TVector<float, 3> StartLocal = TM.InverseTransformPositionNoScale(StartPose.TransformPositionNoScale(CollisionParticles.X(i)));
		const TVector<float, 3> EndLocal = StartLocal + DirLocal * DeltaMag;

		Pair<TVector<float, 3>, bool> Result = Object->FindClosestIntersection(StartLocal, EndLocal, /*Thickness=*/0.f);

		if(Result.Second)
		{
#if WITH_PHYSX
			const float Distance = (Result.First - StartLocal).Size();
			if(!bFound || Distance < OutHit.distance)
			{
				OutHit.distance = Distance;	//todo(ocohen): assuming physx structs for now
				OutHit.position = U2PVector(TM.TransformPositionNoScale(Result.First));
				const TVector<float, 3> LocalNormal = Object->Normal(Result.First);
				OutHit.normal = U2PVector(TM.TransformVectorNoScale(LocalNormal));
				SetFlags(OutHit, EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position);
			}
			bFound = true;
#endif
		}
	}

	return bFound;
}

bool LowLevelOverlap(const UGeometryCollectionComponent& GeomCollectionComponent, const TArray<int32>& InPotentialIntersections, const Chaos::TClusterBuffer<float, 3>& ClusterBuffer, const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& GeomPose, FHitOverlap& OutHit)
{
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_LowLevelOverlap);

	const FGeometryCollectionPhysicsProxy* PhysObject = GeomCollectionComponent.GetPhysicsProxy();
	if (!ensure(PhysObject))
	{
		return false;
	}
	const FGeometryCollectionResults& PhysResult = PhysObject->GetPhysicsResults().GetGameDataForRead();

	const TManagedArray<int32>& RigidBodyIdArray = PhysResult.RigidBodyIds;
	const TManagedArray<FTransform>& TransformArray = PhysResult.Transforms;
	const TArray<bool>& DisabledFlags = PhysResult.DisabledStates;
	
	bool bFound = false;

	if (Chaos::FPhysicsSolver* Solver = GeomCollectionComponent.ChaosSolverActor != nullptr ? GeomCollectionComponent.ChaosSolverActor->GetSolver() : GeomCollectionComponent.GetOwner()->GetWorld()->PhysicsScene_Chaos->GetSolver())
	{
		const TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();	//todo(ocohen): should these just get passed in instead of hopping through scene?

		check(QueryGeom.HasBoundingBox()); // We do not support unbounded query objects
		
		PhysicsParallelFor(InPotentialIntersections.Num(), [&](int32 PotentialIdx)
		{
			int32 RigidBodyIdx = InPotentialIntersections[PotentialIdx];
			if (!IsValidIndexAndTransform(PhysResult, Particles, TransformArray, DisabledFlags, RigidBodyIdx, false))
			{
				return;
			}

			const int32 LocalBodyIndex = RigidBodyIdx - PhysResult.BaseIndex;
			const TRigidTransform<float, 3>& TM = PhysResult.ParticleToWorldTransforms[LocalBodyIndex];

			const TImplicitObject<float, 3>* Object = ClusterBuffer.GeometryPtrs[RigidBodyIdx].Get();
			if (!Object)
			{
				return;
			}

			// Need to do narrow phase
			Pair<TVector<float, 3>, bool> Result = QueryGeom.FindDeepestIntersection(Object, Particles.CollisionParticles(RigidBodyIdx).Get(), TRigidTransform<float, 3>(TM) * TRigidTransform<float, 3>(GeomPose).Inverse(), 0);

			if (Result.Second)
			{
				bFound = true;
			}
		});
	}
	return bFound;
}

int32 UseSlowSQ = 0;
FAutoConsoleVariableRef CVarUseSlowSQ(TEXT("p.UseSlowSQ"), UseSlowSQ, TEXT(""));

void FGeometryCollectionSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	SCOPE_CYCLE_COUNTER(STAT_GCRaycast);
	FChaosScopeSolverLock SolverScopeLock;

	using namespace Chaos;

	FChaosSolversModule* Module = FChaosSolversModule::GetModule();

	TMap<const Chaos::FPhysicsSolver*, TArray<int32>> SolverIntersectionSets;

	Chaos::TSpatialRay<float, 3> Ray(Start, Start + Dir * DeltaMagnitude);

#if WITH_PHYSX

	const TArray<Chaos::FPhysicsSolver*>& Solvers = Module->GetSolvers();

	for(const Chaos::FPhysicsSolver* Solver : Solvers)
	{
		if (!Solver)
		{
			continue;
		}

		TArray<int32> IntersectionSet = Solver->GetSpatialAcceleration()->FindAllIntersections(Ray);
		Solver->ReleaseSpatialAcceleration();

		const Chaos::TClusterBuffer<float, 3>& Buffer = Solver->GetRigidClustering().GetBufferedData();


		const Chaos::FPhysicsSolver::FPhysicsProxyReverseMapping& ObjectMap = Solver->GetPhysicsProxyReverseMapping_GameThread();

		FHitRaycast Hit;
		int32 IntersectionSetSize = IntersectionSet.Num();
		for(int32 i = 0; i < IntersectionSet.Num(); ++i)
		{
			const int32 IntersectParticleIndex = IntersectionSet[i];
			const PhysicsProxyWrapper& ObjectWrapper = ObjectMap.PhysicsProxyReverseMappingArray[IntersectParticleIndex];
			if (!ObjectWrapper.PhysicsProxy)
			{
				const TImplicitObject<float, 3>* Object = Buffer.GeometryPtrs[IntersectParticleIndex].Get();
				// Ignore the ground plane
				if (IntersectParticleIndex == 0 && Object->GetType(true) == ImplicitObjectType::Plane)
				{
					continue;
				}
				if (Object && !UseSlowSQ && Object->IsUnderlyingUnion())
				{
					const TImplicitObjectUnion<float, 3>* Union = static_cast<const TImplicitObjectUnion<float, 3>*>(Object);
					//hack: this is terrible because we have no buffered transform so could be off, but most of the time these things are static

					{
						const TRigidTransform<float, 3>* TMPtr = Buffer.ClusterParentTransforms.Find(IntersectParticleIndex);

						if (ensure(TMPtr))
						{
							if (!(ensure(!FMath::IsNaN(TMPtr->GetTranslation().X)) && ensure(!FMath::IsNaN(TMPtr->GetTranslation().Y)) && ensure(!FMath::IsNaN(TMPtr->GetTranslation().Z))))
							{
								continue;
							}

							const TVector<float, 3> StartLocal = TMPtr->InverseTransformPositionNoScale(Start);
							const TVector<float, 3> DirLocal = TMPtr->InverseTransformVectorNoScale(Dir);
							const TVector<float, 3> EndLocal = StartLocal + DirLocal * DeltaMagnitude;
							Chaos::TSpatialRay<float, 3> LocalRay(StartLocal, EndLocal);
							const TArray<int32> IntersectingChildren = Union->FindAllIntersectingChildren(LocalRay);
							IntersectionSet.Append(IntersectingChildren);
						}
						else
						{
							UE_LOG(LogChaos, Warning, TEXT("SQ: Could not find a valid transform for a cluster parent for faster child intersections."));
						}
					}
				}
				else
				{
					if (ensure(Buffer.MChildren.Contains(IntersectParticleIndex)))
					{
						const TArray<uint32>& Children = Buffer.MChildren[IntersectParticleIndex];
						for (const uint32 Child : Children)
						{
							IntersectionSet.Add(Child);
						}
					}
				}
				continue;
			}

			if(ObjectWrapper.Type == EPhysicsProxyType::GeometryCollectionType && ensure(ObjectWrapper.PhysicsProxy))
			{
				LowLevelRaycastSingleElement(IntersectParticleIndex, Solver, Buffer, static_cast<FGeometryCollectionPhysicsProxy*>(ObjectWrapper.PhysicsProxy), Start, Dir, DeltaMagnitude, i >= IntersectionSetSize, OutputFlags, Hit);

				// If we registered a hit
				if(Hit.distance != PX_MAX_REAL && ensure(ObjectWrapper.PhysicsProxy))
				{
#if !WITH_IMMEDIATE_PHYSX && PHYSICS_INTERFACE_PHYSX
					//todo(ocohen):hack placeholder while we convert over to non physx API
					UGeometryCollectionComponent* Component = Cast<UGeometryCollectionComponent>(ObjectWrapper.PhysicsProxy->GetOwner());
					check(Component);
					if (Component->IsRegistered())
					{
						const FPhysicsActorHandle& ActorHandle = Component->DummyBodyInstance.GetPhysicsActorHandle();
						PxRigidActor* PRigidActor = ActorHandle.SyncActor;
						uint32 PNumShapes = PRigidActor->getNbShapes();
						TArray<PxShape*> PShapes;
						PShapes.AddZeroed(PNumShapes);
						PRigidActor->getShapes(PShapes.GetData(), sizeof(PShapes[0]) * PNumShapes);
						SetActor(Hit, ActorHandle.SyncActor);
						SetShape(Hit, PShapes[0]);
					}
#else
					check(false);	//this can't actually return nullptr since higher up API assumes both shape and actor exists in the low level
					SetActor(Hit, nullptr);
					SetShape(Hit, nullptr);	//todo(ocohen): what do we return for apeiron?
#endif
					Insert(HitBuffer, Hit, true);	//for now assume all blocking hits
				}
			}
		}

		Solver->GetRigidClustering().ReleaseBufferedData();
		Solver->ReleasePhysicsProxyReverseMapping();
	}

#endif
}

DECLARE_CYCLE_STAT(TEXT("Sweep Broadphase"), STAT_SQSweepBroadPhase, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Sweep Narrowphase"), STAT_SQSweepNarrowPhase, STATGROUP_Chaos);

//@todo(mlentine): Avoid duplicated code between this and overlap
void FGeometryCollectionSQAccelerator::Sweep(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	SCOPE_CYCLE_COUNTER(STAT_GCSweep);
	FChaosScopeSolverLock SolverScopeLock;

	using namespace Chaos;
#if WITH_PHYSX

	TMap<const Chaos::FPhysicsSolver*, TArray<int32>> SolverIntersectionSets;

	// Getter for intersections from the mapping above
	auto GetIntersectionsFunc = [&](const Chaos::FPhysicsSolver* InSolver, const Chaos::TParticles<float, 3>& InCollisionParticles, float InDeltaMag, const FTransform& InPose) -> TArray<int32>
	{
		SCOPE_CYCLE_COUNTER(STAT_SQSweepBroadPhase)

		TArray<int32> PotentialIntersections;

		if(InSolver->Enabled())
		{
			const Chaos::ISpatialAcceleration<float, 3>* SpacialAcceleration = InSolver->GetSpatialAcceleration();

			const int32 NumCollisionParticles = InCollisionParticles.Size();
			for(int32 ParticleIndex = 0; ParticleIndex < NumCollisionParticles; ++ParticleIndex)
			{
				const Chaos::TVector<float, 3> RayStart = InPose.TransformPositionNoScale(InCollisionParticles.X(ParticleIndex));
				const Chaos::TVector<float, 3> RayEnd = RayStart + Dir * InDeltaMag;

				PotentialIntersections.Append(SpacialAcceleration->FindAllIntersections(Chaos::TSpatialRay<float, 3>(RayStart, RayEnd)));
			}

			InSolver->ReleaseSpatialAcceleration();

			PotentialIntersections.Sort();

			for(int32 i = PotentialIntersections.Num() - 1; i > 0; i--)
			{
				if(PotentialIntersections[i] == PotentialIntersections[i - 1])
				{
					PotentialIntersections.RemoveAtSwap(i, 1, false);
				}
			}
		}

		return PotentialIntersections;
	};

	// Need somewhere to store our translated shape, similar to the PhysX geom holder
	struct FLocalImplicitStorage
	{
		FLocalImplicitStorage()
			: Capsule(FVector::ZeroVector, FVector::ZeroVector, 0.0f)
			, Sphere(FVector::ZeroVector, 0.0f)
			, Box(FVector::ZeroVector, FVector::ZeroVector)
		{}

		Chaos::TCapsule<float> Capsule;
		Chaos::TSphere<float, 3> Sphere;
		Chaos::TBox<float, 3> Box;
	};
	FLocalImplicitStorage ImplicitStorage;
	Chaos::TImplicitObject<float, 3>* Implicit = nullptr;

	bool bHit = false;
	FHitSweep Hit;
	PxGeometryHolder Holder(QueryGeom);
	Chaos::TParticles<float, 3> CollisionParticles;

	if(Holder.getType() == PxGeometryType::eCAPSULE)
	{
		physx::PxCapsuleGeometry& PxCapsule = Holder.capsule();
		
		float Radius = PxCapsule.radius;
		float HalfHeight = PxCapsule.halfHeight;
		Chaos::TVector<float, 3> x1(-HalfHeight, 0, 0);
		Chaos::TVector<float, 3> x2(HalfHeight, 0, 0);
		
		Chaos::TCapsule<float> Capsule(x1, x2, Radius);
		ImplicitStorage.Capsule = MoveTemp(Capsule);
		Implicit = &ImplicitStorage.Capsule;

		{
			CollisionParticles.AddParticles(14);
			CollisionParticles.X(0) = Chaos::TVector<float, 3>(HalfHeight + Radius, 0, 0);
			CollisionParticles.X(1) = Chaos::TVector<float, 3>(-HalfHeight - Radius, 0, 0);
			CollisionParticles.X(2) = Chaos::TVector<float, 3>(HalfHeight, Radius, Radius);
			CollisionParticles.X(3) = Chaos::TVector<float, 3>(HalfHeight, -Radius, Radius);
			CollisionParticles.X(4) = Chaos::TVector<float, 3>(HalfHeight, -Radius, -Radius);
			CollisionParticles.X(5) = Chaos::TVector<float, 3>(HalfHeight, Radius, -Radius);
			CollisionParticles.X(6) = Chaos::TVector<float, 3>(0, Radius, Radius);
			CollisionParticles.X(7) = Chaos::TVector<float, 3>(0, -Radius, Radius);
			CollisionParticles.X(8) = Chaos::TVector<float, 3>(0, -Radius, -Radius);
			CollisionParticles.X(9) = Chaos::TVector<float, 3>(0, Radius, -Radius);
			CollisionParticles.X(10) = Chaos::TVector<float, 3>(-HalfHeight, Radius, Radius);
			CollisionParticles.X(11) = Chaos::TVector<float, 3>(-HalfHeight, -Radius, Radius);
			CollisionParticles.X(12) = Chaos::TVector<float, 3>(-HalfHeight, -Radius, -Radius);
			CollisionParticles.X(13) = Chaos::TVector<float, 3>(-HalfHeight, Radius, -Radius);
		}
	}
	else if(Holder.getType() == PxGeometryType::eSPHERE)
	{
		physx::PxSphereGeometry& PxSphere = Holder.sphere();

		float Radius = PxSphere.radius;

		Chaos::TSphere<float, 3> Sphere(Chaos::TVector<float, 3>(0), Radius);
		ImplicitStorage.Sphere = MoveTemp(Sphere);
		Implicit = &ImplicitStorage.Sphere;

		{
			CollisionParticles.AddParticles(6);
			CollisionParticles.X(0) = Chaos::TVector<float, 3>(Radius, 0, 0);
			CollisionParticles.X(1) = Chaos::TVector<float, 3>(-Radius, 0, 0);
			CollisionParticles.X(2) = Chaos::TVector<float, 3>(0, Radius, Radius);
			CollisionParticles.X(3) = Chaos::TVector<float, 3>(0, -Radius, Radius);
			CollisionParticles.X(4) = Chaos::TVector<float, 3>(0, -Radius, -Radius);
			CollisionParticles.X(5) = Chaos::TVector<float, 3>(0, Radius, -Radius);
		}
	}
	else if(Holder.getType() == PxGeometryType::eBOX)
	{
		physx::PxBoxGeometry& PxBox = Holder.box();

		Chaos::TVector<float, 3> x1(-P2UVector(PxBox.halfExtents));
		Chaos::TVector<float, 3> x2(-x1);

		Chaos::TBox<float, 3> Box(x1, x2);
		ImplicitStorage.Box = MoveTemp(Box);
		Implicit = &ImplicitStorage.Box;

		{
			CollisionParticles.AddParticles(8);
			CollisionParticles.X(0) = Chaos::TVector<float, 3>(x1.X, x1.Y, x1.Z);
			CollisionParticles.X(1) = Chaos::TVector<float, 3>(x1.X, x1.Y, x2.Z);
			CollisionParticles.X(2) = Chaos::TVector<float, 3>(x1.X, x2.Y, x1.Z);
			CollisionParticles.X(3) = Chaos::TVector<float, 3>(x2.X, x1.Y, x1.Z);
			CollisionParticles.X(4) = Chaos::TVector<float, 3>(x2.X, x2.Y, x2.Z);
			CollisionParticles.X(5) = Chaos::TVector<float, 3>(x2.X, x2.Y, x1.Z);
			CollisionParticles.X(6) = Chaos::TVector<float, 3>(x2.X, x1.Y, x2.Z);
			CollisionParticles.X(7) = Chaos::TVector<float, 3>(x1.X, x2.Y, x2.Z);
		}
	}
	else
	{
		// We don't support anything else for sweeps
		ensureMsgf(false, TEXT("Unsupported query type used for sweep"));
		return;
	}

	const TArray<Chaos::FPhysicsSolver*>& Solvers = FChaosSolversModule::GetModule()->GetSolvers();

	for(const Chaos::FPhysicsSolver* Solver : Solvers)
	{		
		if (!Solver)
		{
			continue;
		}

		TArray<int32> IntersectionSet = GetIntersectionsFunc(Solver, CollisionParticles, DeltaMag, StartTM);

		const Chaos::FPhysicsSolver::FPhysicsProxyReverseMapping& ObjectMap = Solver->GetPhysicsProxyReverseMapping_GameThread();
		const Chaos::TClusterBuffer<float, 3>& Buffer = Solver->GetRigidClustering().GetBufferedData();


		int32 IntersectionSetSize = IntersectionSet.Num();
		for(int32 i = 0; i < IntersectionSet.Num(); ++i)
		{
			SCOPE_CYCLE_COUNTER(STAT_SQSweepNarrowPhase)

			const int32 ParticleIndex = IntersectionSet[i];
			const PhysicsProxyWrapper& ObjectWrapper = ObjectMap.PhysicsProxyReverseMappingArray[ParticleIndex];

			if (!ObjectWrapper.PhysicsProxy)
			{
				const TImplicitObject<float, 3>* Object = Buffer.GeometryPtrs[ParticleIndex].Get();
				
				// Ignore ground plane
				if (ParticleIndex == 0 && Object->GetType(true) == ImplicitObjectType::Plane)
				{
					continue;
				}
				if (Object && !UseSlowSQ && Object->IsUnderlyingUnion())
				{
					const TImplicitObjectUnion<float, 3>* Union = static_cast<const TImplicitObjectUnion<float, 3>*>(Object);
					//hack: this is terrible because we have no buffered transform so could be off, but most of the time these things are static

					{
						const TRigidTransform<float, 3>* TMPtr = Buffer.ClusterParentTransforms.Find(ParticleIndex);

						if (ensure(TMPtr))
						{
							if (!(ensure(!FMath::IsNaN(TMPtr->GetTranslation().X)) && ensure(!FMath::IsNaN(TMPtr->GetTranslation().Y)) && ensure(!FMath::IsNaN(TMPtr->GetTranslation().Z))))
							{
								continue;
							}

							const TVector<float, 3> StartLocal = TMPtr->InverseTransformPositionNoScale(StartTM.GetLocation());
							const TVector<float, 3> DirLocal = TMPtr->InverseTransformVectorNoScale(Dir);
							const TVector<float, 3> EndLocal = StartLocal + DirLocal * DeltaMag;
							Chaos::TSpatialRay<float, 3> LocalRay(StartLocal, EndLocal);
							const TArray<int32> IntersectingChildren = Union->FindAllIntersectingChildren(LocalRay);
							IntersectionSet.Append(IntersectingChildren);
						}
						else
						{
							UE_LOG(LogChaos, Warning, TEXT("SQ: Could not find a valid transform for a cluster parent for faster child intersections."));
						}
					}
				}
				else
				{
					if (ensure(Buffer.MChildren.Contains(ParticleIndex)))
					{
						const TArray<uint32>& Children = Buffer.MChildren[ParticleIndex];
						for (const uint32 Child : Children)
						{
							IntersectionSet.Add(Child);
						}
					}
				}
				continue;
			}

			if(ObjectWrapper.Type == EPhysicsProxyType::GeometryCollectionType && ensure(ObjectWrapper.PhysicsProxy))
			{
				if(LowLevelSweepSingleElement(ParticleIndex, Solver, Buffer, static_cast<FGeometryCollectionPhysicsProxy*>(ObjectWrapper.PhysicsProxy), *Implicit, CollisionParticles, StartTM, Dir, DeltaMag, i >= IntersectionSetSize, Hit))
				{
#if !WITH_IMMEDIATE_PHYSX && PHYSICS_INTERFACE_PHYSX
					//todo(mlentine): This is duplicated from above and should be merged
					//todo(ocohen):hack placeholder while we convert over to non physx API
					UGeometryCollectionComponent* Component = Cast<UGeometryCollectionComponent>(ObjectWrapper.PhysicsProxy->GetOwner());
					check(Component);

					if (Component->IsRegistered())
					{
						const FPhysicsActorHandle& ActorHandle = Component->DummyBodyInstance.GetPhysicsActorHandle();
						PxRigidActor* PRigidActor = ActorHandle.SyncActor;
						uint32 PNumShapes = PRigidActor->getNbShapes();
						TArray<PxShape*> PShapes;
						PShapes.AddZeroed(PNumShapes);
						PRigidActor->getShapes(PShapes.GetData(), sizeof(PShapes[0]) * PNumShapes);
						SetActor(Hit, ActorHandle.SyncActor);
						SetShape(Hit, PShapes[0]);
#else
						check(false);	//this can't actually return nullptr since higher up API assumes both shape and actor exists in the low level
						SetActor(Hit, nullptr);
						SetShape(Hit, nullptr);	//todo(ocohen): what do we return for apeiron?
#endif
						Insert(HitBuffer, Hit, true);	//for now assume all blocking hits
					}
				}
			}
		}

		Solver->GetRigidClustering().ReleaseBufferedData();

		Solver->ReleasePhysicsProxyReverseMapping();
	}
#endif
}

bool LowLevelOverlapSingleElement(int32 InParticleIndex, const Chaos::FPhysicsSolver* InSolver, const Chaos::TClusterBuffer<float, 3>& ClusterBuffer, const FGeometryCollectionPhysicsProxy* InObject, const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& InPose, FHitOverlap& OutHit)
{
	using namespace Chaos;

	checkSlow(InSolver);
	checkSlow(InObject);

	const FGeometryCollectionResults& PhysResult = InObject->GetPhysicsResults().GetGameDataForRead();

	const TManagedArray<int32>& RigidBodyIdArray = PhysResult.RigidBodyIds;
	const TManagedArray<FTransform>& TransformArray = PhysResult.Transforms;
	const TArray<bool>& DisabledFlags = PhysResult.DisabledStates;

	const TPBDRigidParticles<float, 3>& Particles = InSolver->GetRigidParticles();

	if(!IsValidIndexAndTransform(PhysResult, Particles, TransformArray, DisabledFlags, InParticleIndex, false))
	{
		return false;
	}

	const int32 LocalBodyIndex = InParticleIndex - PhysResult.BaseIndex;
	const TRigidTransform<float, 3>& TM = PhysResult.ParticleToWorldTransforms[LocalBodyIndex];
	const TImplicitObject<float, 3>* Object = ClusterBuffer.GeometryPtrs[InParticleIndex].Get();
	
	if(!Object)
	{
		return false;
	}

	Pair<TVector<float, 3>, bool> Result = QueryGeom.FindDeepestIntersection(Object, Particles.CollisionParticles(InParticleIndex).Get(), TRigidTransform<float, 3>(TM) * TRigidTransform<float, 3>(InPose).Inverse(), 0);

	return Result.Second;
}


void FGeometryCollectionSQAccelerator::Overlap(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	return;	//todo: This is currently broken because it doesn'thandle cluster unions. Need to fix function - disabling entirely for now
	SCOPE_CYCLE_COUNTER(STAT_GCOverlap);
	FChaosScopeSolverLock SolverScopeLock;

#if WITH_PHYSX

	TMap<const Chaos::FPhysicsSolver*, TArray<int32>> SolverIntersectionSets;

	// Getter for intersections from the mapping above
	auto GetIntersectionsFunc = [&](const Chaos::FPhysicsSolver* InSolver, Chaos::TImplicitObject<float, 3>* InGeometry, const FTransform& InPose) -> TArray<int32>*
	{
		TArray<int32>* CurrentIntersections = SolverIntersectionSets.Find(InSolver);

		if(!CurrentIntersections)
		{
			// This is safe to access here, it's buffered from the physics thread
			SolverIntersectionSets.Add(InSolver, InSolver->GetSpatialAcceleration()->FindAllIntersections(InGeometry->BoundingBox().TransformedBox(GeomPose)));
			InSolver->ReleaseSpatialAcceleration();
		}

		return SolverIntersectionSets.Find(InSolver);
	};

	// Need somewhere to store our translated shape, similar to the PhysX geom holder
	struct FLocalImplicitStorage
	{
		FLocalImplicitStorage()
			: Capsule(FVector::ZeroVector, FVector::ZeroVector, 0.0f)
			, Sphere(FVector::ZeroVector, 0.0f)
			, Box(FVector::ZeroVector, FVector::ZeroVector)
		{}

		Chaos::TCapsule<float> Capsule;
		Chaos::TSphere<float, 3> Sphere;
		Chaos::TBox<float, 3> Box;
	};
	FLocalImplicitStorage ImplicitStorage;

	bool bHit = false;
	FHitOverlap Hit;
	PxGeometryHolder Holder(QueryGeom);
	Chaos::TImplicitObject<float, 3>* Implicit = nullptr;

	if(Holder.getType() == PxGeometryType::eCAPSULE)
	{
		physx::PxCapsuleGeometry& PxCapsule = Holder.capsule();
		float Radius = PxCapsule.radius;
		float HalfHeight = PxCapsule.halfHeight - Radius;
		Chaos::TVector<float, 3> x1(-HalfHeight, 0, 0);
		Chaos::TVector<float, 3> x2(HalfHeight, 0, 0);
		Chaos::TCapsule<float> Capsule(x1, x2, Radius);
		ImplicitStorage.Capsule = MoveTemp(Capsule);
		Implicit = &ImplicitStorage.Capsule;
	}
	else if(Holder.getType() == PxGeometryType::eSPHERE)
	{
		physx::PxSphereGeometry& PxSphere = Holder.sphere();
		float Radius = PxSphere.radius;
		Chaos::TSphere<float, 3> Sphere(Chaos::TVector<float, 3>(0), Radius);
		ImplicitStorage.Sphere = MoveTemp(Sphere);
		Implicit = &ImplicitStorage.Sphere;
	}
	else if(Holder.getType() == PxGeometryType::eBOX)
	{
		physx::PxBoxGeometry& PxBox = Holder.box();
		Chaos::TVector<float, 3> x1(-P2UVector(PxBox.halfExtents));
		Chaos::TVector<float, 3> x2(-x1);
		Chaos::TBox<float, 3> Box(x1, x2);
		ImplicitStorage.Box = MoveTemp(Box);
		Implicit = &ImplicitStorage.Box;
	}
	else
	{
		ensureMsgf(false, TEXT("Unsupported query type used for overlap"));
		return;
	}

	FChaosSolversModule* Module = FChaosSolversModule::GetModule();
	const TArray<Chaos::FPhysicsSolver*> Solvers = Module->GetSolvers();

	for(const Chaos::FPhysicsSolver* Solver : Solvers)
	{
		// Collect all intersections for this solver
		TArray<int32> IntersectionSet = Solver->GetSpatialAcceleration()->FindAllIntersections(Implicit->BoundingBox().TransformedBox(GeomPose));
		Solver->ReleaseSpatialAcceleration();

		const Chaos::FPhysicsSolver::FPhysicsProxyReverseMapping& ObjectMap = Solver->GetPhysicsProxyReverseMapping_GameThread();

		const Chaos::TClusterBuffer<float, 3>& ClusterBuffer = Solver->GetRigidClustering().GetBufferedData();

		for(int32 i = 0; i < IntersectionSet.Num(); ++i)
		{
			const int32 IntersectParticleIndex = IntersectionSet[i];
			const PhysicsProxyWrapper& ObjectWrapper = ObjectMap.PhysicsProxyReverseMappingArray[IntersectParticleIndex];

			if(ObjectWrapper.Type == EPhysicsProxyType::GeometryCollectionType && ensure(ObjectWrapper.PhysicsProxy))
			{
				if(LowLevelOverlapSingleElement(IntersectParticleIndex, Solver, ClusterBuffer, static_cast<FGeometryCollectionPhysicsProxy*>(ObjectWrapper.PhysicsProxy), *Implicit, GeomPose, Hit))
				{
#if !WITH_IMMEDIATE_PHYSX && PHYSICS_INTERFACE_PHYSX
					//todo(mlentine): This is duplicated from above and should be merged
					//todo(ocohen):hack placeholder while we convert over to non physx API
					UGeometryCollectionComponent* Component = Cast<UGeometryCollectionComponent>(ObjectWrapper.PhysicsProxy->GetOwner());
					check(Component);

					if (Component->IsRegistered())
					{
						const FPhysicsActorHandle& ActorHandle = Component->DummyBodyInstance.GetPhysicsActorHandle();
						PxRigidActor* PRigidActor = ActorHandle.SyncActor;
						uint32 PNumShapes = PRigidActor->getNbShapes();
						TArray<PxShape*> PShapes;
						PShapes.AddZeroed(PNumShapes);
						PRigidActor->getShapes(PShapes.GetData(), sizeof(PShapes[0]) * PNumShapes);
						SetActor(Hit, ActorHandle.SyncActor);
						SetShape(Hit, PShapes[0]);
#else
						check(false);	//this can't actually return nullptr since higher up API assumes both shape and actor exists in the low level
						SetActor(Hit, nullptr);
						SetShape(Hit, nullptr);	//todo(ocohen): what do we return for apeiron?
#endif
						InsertOverlap(HitBuffer, Hit);
					}
				}
			}
		}

		Solver->GetRigidClustering().ReleaseBufferedData();
		Solver->ReleasePhysicsProxyReverseMapping();
	}

#endif
}

#endif // TODO_REIMPLEMENT_SCENEQUERY_CROSSENGINE

void FGeometryCollectionSQAccelerator::AddComponent(UGeometryCollectionComponent* Component)
{
	if (ensure(Component))
	{
//		UE_LOG(LogTemp, Log, TEXT("SQAccel: Adding %s (Outer %s)"), *GetNameSafe(Component), *GetNameSafe(Component->GetOuter()));
		Components.Add(Component);
	}
}

void FGeometryCollectionSQAccelerator::RemoveComponent(UGeometryCollectionComponent* Component)
{
	if (ensure(Component))
	{
//		UE_LOG(LogTemp, Log, TEXT("SQAccel: Removing %s (Outer %s)"), *GetNameSafe(Component), *GetNameSafe(Component->GetOuter()));
		Components.Remove(Component);
	}
}
#endif
