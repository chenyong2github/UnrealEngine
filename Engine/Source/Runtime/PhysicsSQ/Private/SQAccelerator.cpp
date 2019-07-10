// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SQAccelerator.h"
#include "CollisionQueryFilterCallbackCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "SceneQueryPhysXImp.h"	//todo: use nice platform wrapper
#include "PhysXInterfaceWrapperCore.h"
#elif PHYSICS_INTERFACE_LLIMMEDIATE
//#include "Collision/Experimental/SceneQueryLLImmediateImp.h"
#endif
#if WITH_CHAOS
#include "Experimental/SceneQueryChaosImp.h"
#endif

#include "Chaos/Particles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/GeometryQueries.h"

FSQAcceleratorEntry* FSQAccelerator::AddEntry(void* Payload)
{
	if (Nodes.Num() == 0)
	{
		Nodes.Add(new FSQNode());
	}

	FSQAcceleratorEntry* NewEntry = new FSQAcceleratorEntry(Payload);
	Nodes[0]->Entries.Add(NewEntry);
	return NewEntry;
}

#if 0
void FSQAccelerator::UpdateBounds(FSQAcceleratorEntry* Entry, const FBoxSphereBounds& NewBounds)
{
	Entry->Bounds = NewBounds;
}
#endif

void FSQAccelerator::RemoveEntry(FSQAcceleratorEntry* Entry)
{
	if (Nodes.Num())
	{
		Nodes[0]->Entries.RemoveSingleSwap(Entry);
	}
	delete Entry;
}

void FSQAccelerator::GetNodes(TArray<const FSQNode*>& NodesFound) const
{
	NodesFound.Reset(Nodes.Num());
	for (const FSQNode* Node : Nodes)
	{
		NodesFound.Add(Node);
	}
}

FSQAccelerator::~FSQAccelerator()
{
	for (FSQNode* Node : Nodes)
	{
		for (FSQAcceleratorEntry* Entry : Node->Entries)
		{
			delete Entry;
		}

		delete Node;
	}
}

template <typename HitType>
class FSQOverlapBuffer
{
public:

	FSQOverlapBuffer(int32 InMaxNumOverlaps)
		: MaxNumOverlaps(InMaxNumOverlaps)
	{
	}

	bool Insert(const HitType& Hit)
	{
		//maybe prioritize based on penetration depth?
		if (Overlapping.Num() < MaxNumOverlaps)
		{
			Overlapping.Add(Hit);
		}

		return Overlapping.Num() < MaxNumOverlaps;
	}

private:
	TArray<HitType>	Overlapping;	//todo(ocohen): use a TInlineArray + compatible bytes to avoid default constructor and allocations
	int32 MaxNumOverlaps;
};

template <typename HitType>
class FSQTraceBuffer
{
public:

	FSQTraceBuffer(float InDeltaMag, int32 InMaxNumOverlaps)
		: DeltaMag(InDeltaMag)
		, MaxNumOverlaps(InMaxNumOverlaps)
	{
	}

	bool Insert(const HitType& Hit, bool bBlocking)
	{
		if (GetDistance(Hit) < DeltaMag)
		{
			if (bBlocking)
			{
				BlockingHit = Hit;
				bHasBlocking = true;
				DeltaMag = GetDistance(Hit);
			}
			else
			{
				//todo(ocohen):use a priority queue or something more sensible than just a full sort every time. Avoid add then remove if possible
				Overlapping.Add(Hit);
				Overlapping.Sort();
				if (Overlapping.Num() > MaxNumOverlaps)
				{
					Overlapping.SetNum(MaxNumOverlaps);
				}
			}
		}

		return true;
	}

	float GetBlockingDistance() const { return DeltaMag; }
	float GetOverlapingDistance() const { return DeltaMag;  /*todo(ocohen): if we keep overlaps sorted we could make this tighter and avoid memory growth.*/ }

private:
	HitType BlockingHit;
	TArray<HitType>	Overlapping;	//todo(ocohen): use a TInlineArray + compatible bytes to avoid default constructor and allocations
	int32 MaxNumOverlaps;
	bool bHasBlocking;
	float DeltaMag;
};

void FSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
#if WITH_PHYSX
	FPhysicsRaycastInputAdapater Inputs(Start, Dir, OutputFlags);

	for (const FSQNode* Node : Nodes)
	{
		//in theory we'd do some checks against max block and max overlap distance to avoid some of these nodes
		for (const FSQAcceleratorEntry* Entry : Node->Entries)
		{
			if (FPhysicsActor* RigidActor = (FPhysicsActor*)Entry->GetPayload())
			{
				const FPhysicsTransform ActorTM = GetGlobalPose(*RigidActor);
				const uint32 NumShapes = GetNumShapes(*RigidActor);
				TArray<FPhysicsShape*, TInlineAllocator<16>> Shapes;
				Shapes.AddZeroed(NumShapes);
				GetShapes(*RigidActor, Shapes.GetData(), NumShapes);
				FHitRaycast Hit;
				for (FPhysicsShape* Shape : Shapes)
				{
					const FCollisionFilterData ShapeFilterData = GetQueryFilterData(*Shape);
					ECollisionQueryHitType FilterType = QueryFlags & EQueryFlags::PreFilter ? QueryCallback.PreFilter(QueryFilter, *Shape, *RigidActor) : ECollisionQueryHitType::Block;	//todo(ocohen): we always use preFilter, should we add a cheaper test?
					if (FilterType != ECollisionQueryHitType::None)
					{
						if (LowLevelRaycastImp(Inputs.Start, Inputs.Dir, DeltaMagnitude, *Shape, ActorTM, Inputs.OutputFlags, Hit))
						{
							SetActor(Hit, RigidActor);
							SetShape(Hit, Shape);
							FilterType = QueryFlags & EQueryFlags::PostFilter ? QueryCallback.PostFilter(QueryFilter, Hit) : FilterType;
							if (FilterType != ECollisionQueryHitType::None)
							{
								Insert(HitBuffer, Hit, FilterType == ECollisionQueryHitType::Block || (QueryFlags & EQueryFlags::AnyHit));
							}
						}
					}
				}
			}
		}	
	}
#endif
}

void FSQAccelerator::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
#if WITH_PHYSX
	FPhysicsSweepInputAdapater Inputs(StartTM, Dir, OutputFlags);
	for (const FSQNode* Node : Nodes)
	{
		for (const FSQAcceleratorEntry* Entry : Node->Entries)
		{
			if (FPhysicsActor* RigidActor = (FPhysicsActor*)Entry->GetPayload())
			{
				const FPhysicsTransform ActorTM = GetGlobalPose(*RigidActor);
				const uint32 NumShapes = GetNumShapes(*RigidActor);
				TArray<FPhysicsShape*, TInlineAllocator<16>> Shapes;
				Shapes.AddZeroed(NumShapes);
				GetShapes(*RigidActor, Shapes.GetData(), NumShapes);
				FHitSweep Hit;
				for (FPhysicsShape* Shape : Shapes)
				{
					const FCollisionFilterData ShapeFilterData = GetQueryFilterData(*Shape);
					ECollisionQueryHitType FilterType = QueryFlags & EQueryFlags::PreFilter ? QueryCallback.PreFilter(QueryFilter, *Shape, *RigidActor) : ECollisionQueryHitType::Block;	//todo(ocohen): we always use preFilter, should we add a cheaper test?
					if (FilterType != ECollisionQueryHitType::None)
					{
						if (LowLevelSweepImp(Inputs.StartTM, Inputs.Dir, DeltaMagnitude, QueryGeom, *Shape, ActorTM, Inputs.OutputFlags, Hit))
						{
							SetActor(Hit, RigidActor);
							SetShape(Hit, Shape);
							FilterType = QueryFlags & EQueryFlags::PostFilter ? QueryCallback.PostFilter(QueryFilter, Hit) : FilterType;
							if (FilterType != ECollisionQueryHitType::None)
							{
								Insert(HitBuffer, Hit, FilterType == ECollisionQueryHitType::Block || (QueryFlags & EQueryFlags::AnyHit));
							}
						}
					}

				}
			}
		}
	}
#endif
}

void FSQAccelerator::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
#if WITH_PHYSX
	FPhysicsOverlapInputAdapater Inputs(GeomPose);
	for (const FSQNode* Node : Nodes)
	{
		for (const FSQAcceleratorEntry* Entry : Node->Entries)
		{
			if (FPhysicsActor* RigidActor = (FPhysicsActor*)Entry->GetPayload())
			{
				const FPhysicsTransform ActorTM = GetGlobalPose(*RigidActor);
				const uint32 NumShapes = GetNumShapes(*RigidActor);
				TArray<FPhysicsShape*, TInlineAllocator<16>> Shapes;
				Shapes.AddZeroed(NumShapes);
				GetShapes(*RigidActor, Shapes.GetData(), NumShapes);
				FHitOverlap Overlap;
				for (FPhysicsShape* Shape : Shapes)
				{
					const FCollisionFilterData ShapeFilterData = GetQueryFilterData(*Shape);
					ECollisionQueryHitType FilterType = QueryFlags & EQueryFlags::PreFilter ? QueryCallback.PreFilter(QueryFilter, *Shape, *RigidActor) : ECollisionQueryHitType::Block;	//todo(ocohen): we always use preFilter, should we add a cheaper test?
					if (FilterType != ECollisionQueryHitType::None)
					{
						if (LowLevelOverlapImp(Inputs.GeomPose, QueryGeom, *Shape, ActorTM, Overlap))
						{
							SetActor(Overlap, RigidActor);
							SetShape(Overlap, Shape);
							FilterType = QueryFlags & EQueryFlags::PostFilter ? QueryCallback.PostFilter(QueryFilter, Overlap) : FilterType;
							if (FilterType != ECollisionQueryHitType::None)
							{
								if (!InsertOverlap(HitBuffer, Overlap))
								{
									return;
								}
							}
						}
					}
				}
			}
		}
	}
#endif
}

void FSQAcceleratorUnion::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Raycast(Start, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFlags, QueryFilter, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Sweep(QueryGeom, StartTM, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFlags, QueryFilter, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Overlap(QueryGeom, GeomPose, HitBuffer, QueryFlags, QueryFilter, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::AddSQAccelerator(ISQAccelerator* InAccelerator)
{
	Accelerators.AddUnique(InAccelerator);
}

void FSQAcceleratorUnion::RemoveSQAccelerator(ISQAccelerator* AcceleratorToRemove)
{
	Accelerators.RemoveSingleSwap(AcceleratorToRemove);	//todo(ocohen): probably want to order these in some optimal way
}

#if INCLUDE_CHAOS
FChaosSQAccelerator::FChaosSQAccelerator(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& InEvolution)
	: Evolution(InEvolution)
{}

struct FPreFilterInfo
{
	const Chaos::TImplicitObject<float, 3>* Geom;
	int32 ActorIdx;
};

void GeneratePrefilterInfo(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& Evolution, const TArray<int32>& PotentialIntersections, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, bool bAnyHit, ICollisionQueryFilterCallbackBase& QueryCallback, TArray<FPreFilterInfo>& OutTouchers, TArray<FPreFilterInfo>& OutBlockers)
{
#if WITH_PHYSX
	using namespace Chaos;
	for (int32 PotentialIdx : PotentialIntersections)
	{
		const TPBDRigidParticles<float, 3>& Particles = Evolution.GetParticles();
		const TImplicitObject<float, 3>* Object = Particles.Geometry(PotentialIdx).Get();
		if (!Object)
		{
			continue;
		}

		TArray<const TImplicitObject<float, 3>*> Geoms;
		if (const TImplicitObjectUnion<float, 3>* UnionObject = Object->template GetObject<const TImplicitObjectUnion<float, 3>>())
		{
			const TArray <TUniquePtr<TImplicitObject<float, 3>>>& UnionGeoms = UnionObject->GetObjects();
			Geoms.Reserve(UnionGeoms.Num());
			for (const auto& GeomPtr : UnionGeoms)
			{
				Geoms.Add(GeomPtr.Get());
			}
		}
		else
		{
			Geoms.Add(Object);
		}

		for (int32 GeomIdx = 0; GeomIdx < Geoms.Num(); ++GeomIdx)
		{
			const TImplicitObject<float, 3>* Geom = Geoms[GeomIdx];
			const ECollisionQueryHitType HitType = QueryFilterData.flags & PxQueryFlag::ePREFILTER ? QueryCallback.PreFilterChaos(QueryFilter, *Geom, PotentialIdx, GeomIdx) : ECollisionQueryHitType::Block;
			if (HitType == ECollisionQueryHitType::Block)
			{
				OutBlockers.Add({ Geom, PotentialIdx });
			}
			else if (HitType == ECollisionQueryHitType::Touch)
			{
				if (bAnyHit)
				{
					OutBlockers.Add({ Geom, PotentialIdx });
				}
				else
				{
					OutTouchers.Add({ Geom, PotentialIdx });
				}
			}
		}
	}
#endif
}

bool ProcessRaycasts(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& Evolution, const TArray<FPreFilterInfo>& PreFilters, const FVector& StartPoint, const FVector& Dir, const float DeltaMag, FPhysicsHitCallback<FRaycastHit>& HitBuffer, EHitFlags OutputFlags, ECollisionQueryHitType HitType, bool bAnyHit)
{
	using namespace Chaos;
	const TPBDRigidParticles<float, 3>& Particles = Evolution.GetParticles();
	for (const FPreFilterInfo& PreFilter : PreFilters)
	{
		const TRigidTransform<float, 3> TM(Particles.X(PreFilter.ActorIdx), Particles.R(PreFilter.ActorIdx));
		const TVector<float, 3> DirLocal = TM.InverseTransformVectorNoScale(Dir);

		FRaycastHit Hit;
		const TVector<float, 3> StartLocal = TM.InverseTransformPositionNoScale(StartPoint);

		TVector<float, 3> LocalNormal;
		TVector<float, 3> LocalPosition;
		float Time;

		if (PreFilter.Geom->Raycast(StartLocal, DirLocal, DeltaMag, /*Thickness=*/0.f, Time, LocalPosition, LocalNormal))
		{
			Hit.Distance = DeltaMag * Time;
			Hit.WorldPosition = TM.TransformPositionNoScale(LocalPosition);
			Hit.WorldNormal = TM.TransformVectorNoScale(LocalNormal);
			Hit.Flags = EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position;

			Hit.ActorIdx = PreFilter.ActorIdx;
			Hit.Shape = PreFilter.Geom;
			Insert(HitBuffer, Hit, HitType == ECollisionQueryHitType::Block);
			if (bAnyHit)
			{
				return false;
			}
		}
	}

	return true;
}


void FChaosSQAccelerator::ChaosRaycast(const FVector& StartPoint, const FVector& Dir, const float DeltaMag, FPhysicsHitCallback<FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
#if WITH_PHYSX
	using namespace Chaos;

	TArray<int32> PotentialIntersections;
	{
		const ISpatialAcceleration<float, 3>& SpatialAcceleration = Evolution.GetSpatialAcceleration();
		const TVector<float, 3>& RayStart = StartPoint;
		const TVector<float, 3> RayEnd = RayStart + Dir * DeltaMag;
		PotentialIntersections = SpatialAcceleration.FindAllIntersections(TSpatialRay<float, 3>(RayStart, RayEnd));
		Evolution.ReleaseSpatialAcceleration();
	}

	TArray<FPreFilterInfo> Touchers;
	TArray<FPreFilterInfo> Blockers;
	const bool bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
	GeneratePrefilterInfo(Evolution, PotentialIntersections, QueryFilter, QueryFilterData, bAnyHit, QueryCallback, Touchers, Blockers);

	const bool bProcessTouches = ProcessRaycasts(Evolution, Blockers, StartPoint, Dir, DeltaMag, HitBuffer, OutputFlags, ECollisionQueryHitType::Block, bAnyHit);
	if (bProcessTouches)
	{
		ProcessRaycasts(Evolution, Touchers, StartPoint, Dir, DeltaMag, HitBuffer, OutputFlags, ECollisionQueryHitType::Touch, bAnyHit);
	}

	FinalizeQuery(HitBuffer);
#endif
}

Chaos::TParticles<float, 3> GenerateSamplePoints(const Chaos::TImplicitObject<float, 3>& QueryGeom, float& OutThickness)
{
	using namespace Chaos;
	TParticles<float, 3> SamplePoints;
	OutThickness = 0.f;
	switch (QueryGeom.GetType())
	{
	case ImplicitObjectType::Sphere:
	{
		const TSphere<float, 3>& Sphere = static_cast<const TSphere<float, 3>&>(QueryGeom);
		const float Radius = Sphere.Radius();
		OutThickness = Radius;

		SamplePoints.AddParticles(1);
		SamplePoints.X(0) = Chaos::TVector<float, 3>(0);
		break;
	}
	case ImplicitObjectType::Box:
	{
		const TBox<float, 3>& Box = static_cast<const TBox<float, 3>&>(QueryGeom);
		Chaos::TVector<float, 3> X1(Box.Extents() * -0.5f);
		Chaos::TVector<float, 3> X2(-X1);

		SamplePoints.AddParticles(8);
		SamplePoints.X(0) = TVector<float, 3>(X1.X, X1.Y, X1.Z);
		SamplePoints.X(1) = TVector<float, 3>(X1.X, X1.Y, X2.Z);
		SamplePoints.X(2) = TVector<float, 3>(X1.X, X2.Y, X1.Z);
		SamplePoints.X(3) = TVector<float, 3>(X2.X, X1.Y, X1.Z);
		SamplePoints.X(4) = TVector<float, 3>(X2.X, X2.Y, X2.Z);
		SamplePoints.X(5) = TVector<float, 3>(X2.X, X2.Y, X1.Z);
		SamplePoints.X(6) = TVector<float, 3>(X2.X, X1.Y, X2.Z);
		SamplePoints.X(7) = TVector<float, 3>(X1.X, X2.Y, X2.Z);
		break;
	}
	case ImplicitObjectType::Capsule:
	{
		const TCapsule<float>& Capsule = static_cast<const TCapsule<float>&>(QueryGeom);
		const float Radius = Capsule.GetRadius();
		OutThickness = Radius;
		const float HalfHeight = Capsule.GetHeight() * 0.5f;

		//to at least be accurate, we add all the points such that the capsule is made up of many spheres which we can currently raycast correctly
		const int32 NumPtsMinusOne = 3 + (HalfHeight / Radius) * 2.f;
		SamplePoints.AddParticles(NumPtsMinusOne);
		for (int32 Idx = 0; Idx < NumPtsMinusOne; ++Idx)
		{
			SamplePoints.X(Idx) = TVector<float, 3>(HalfHeight + Radius - Radius * Idx, 0, 0);
		}
		SamplePoints.AddParticles(1);
		SamplePoints.X(NumPtsMinusOne) = TVector<float, 3>(-HalfHeight - Radius, 0, 0);	//if last sphere didn't go all the way, add one

		/*SamplePoints.AddParticles(14);
		SamplePoints.X(0) = TVector<float, 3>(HalfHeight + Radius, 0, 0);
		SamplePoints.X(1) = TVector<float, 3>(-HalfHeight - Radius, 0, 0);
		SamplePoints.X(2) = TVector<float, 3>(HalfHeight, Radius, Radius);
		SamplePoints.X(3) = TVector<float, 3>(HalfHeight, -Radius, Radius);
		SamplePoints.X(4) = TVector<float, 3>(HalfHeight, -Radius, -Radius);
		SamplePoints.X(5) = TVector<float, 3>(HalfHeight, Radius, -Radius);
		SamplePoints.X(6) = TVector<float, 3>(0, Radius, Radius);
		SamplePoints.X(7) = TVector<float, 3>(0, -Radius, Radius);
		SamplePoints.X(8) = TVector<float, 3>(0, -Radius, -Radius);
		SamplePoints.X(9) = TVector<float, 3>(0, Radius, -Radius);
		SamplePoints.X(10) = TVector<float, 3>(-HalfHeight, Radius, Radius);
		SamplePoints.X(11) = TVector<float, 3>(-HalfHeight, -Radius, Radius);
		SamplePoints.X(12) = TVector<float, 3>(-HalfHeight, -Radius, -Radius);
		SamplePoints.X(13) = TVector<float, 3>(-HalfHeight, Radius, -Radius);*/
		break;
	}
	default:
	{
		ensureMsgf(false, TEXT("Unsupported sweep geometry"));
	}
	}

	return SamplePoints;
}

bool ProcessSweeps(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& Evolution, const TArray<FPreFilterInfo>& PreFilters, const Chaos::TImplicitObject<float,3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float Length, FPhysicsHitCallback<FSweepHit>& HitBuffer, EHitFlags OutputFlags, ECollisionQueryHitType HitType, bool bAnyHit)
{
	using namespace Chaos;
	const TPBDRigidParticles<float, 3>& Particles = Evolution.GetParticles();
	for (const FPreFilterInfo& PreFilter : PreFilters)
	{
		const TRigidTransform<float, 3> TM(Particles.X(PreFilter.ActorIdx), Particles.R(PreFilter.ActorIdx));

		FSweepHit Hit;
		bool bFound = false;
		{
			TVector<float, 3> Normal;
			TVector<float, 3> Position;
			float Time;

			if (SweepQuery<float, 3>(*PreFilter.Geom, TM, QueryGeom, StartTM, Dir, Length, Time, Position, Normal))
			{
				const float Distance = Time * Length;	//assumes distance is the distance traveled by the sweeping geometry, not the distance including the point of intersection
				if (!bFound || Distance < Hit.Distance)
				{
					Hit.Distance = Distance;
					Hit.WorldPosition = Position;
					Hit.WorldNormal = Normal;
					Hit.Flags = EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position;
					bFound = true;
					if (bAnyHit)
					{
						break;
					}
				}
			}
		}

		if (bFound)
		{
			Hit.ActorIdx = PreFilter.ActorIdx;
			Hit.Shape = PreFilter.Geom;
			Insert(HitBuffer, Hit, HitType == ECollisionQueryHitType::Block);
			if (bAnyHit)
			{
				return false;
			}
		}
	}

	return true;
}

void FChaosSQAccelerator::ChaosSweep(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMag, FPhysicsHitCallback<FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
#if WITH_PHYSX
	using namespace Chaos;

	//todo: handle levelset
	//todo: handle sphere/bounds sweep through spatial acceleration
	float Thickness;
	TParticles<float, 3> SamplePoints = GenerateSamplePoints(QueryGeom, Thickness);
	
	TArray<int32> PotentialIntersections;
	{
		const ISpatialAcceleration<float, 3>& SpatialAcceleration = Evolution.GetSpatialAcceleration();
		const int32 NumCollisionParticles = SamplePoints.Size();
		for (int32 ParticleIndex = 0; ParticleIndex < NumCollisionParticles; ++ParticleIndex)
		{
			const TVector<float, 3> RayStart = StartTM.TransformPositionNoScale(SamplePoints.X(ParticleIndex));
			const TVector<float, 3> RayEnd = RayStart + Dir * DeltaMag;

			PotentialIntersections.Append(SpatialAcceleration.FindAllIntersections(TSpatialRay<float, 3>(RayStart, RayEnd)));
		}

		Evolution.ReleaseSpatialAcceleration();
	}
	PotentialIntersections.Sort();	//remove duplicates. Todo: don't use multiple sweeps in broadphase
	int32 PrevInstance = INDEX_NONE;
	for (int32 Idx = PotentialIntersections.Num() - 1; Idx >= 0; --Idx)
	{
		int32 Instance = PotentialIntersections[Idx];
		if (Instance == PrevInstance)
		{
			PotentialIntersections.RemoveAtSwap(Idx);
		}
		PrevInstance = Instance;
	}

	TArray<FPreFilterInfo> Touchers;
	TArray<FPreFilterInfo> Blockers;
	const bool bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
	GeneratePrefilterInfo(Evolution, PotentialIntersections, QueryFilter, QueryFilterData, bAnyHit, QueryCallback, Touchers, Blockers);

	const bool bProcessTouches = ProcessSweeps(Evolution, Blockers, QueryGeom, StartTM, Dir, DeltaMag, HitBuffer, OutputFlags, ECollisionQueryHitType::Block, bAnyHit);
	if (bProcessTouches)
	{
		ProcessSweeps(Evolution, Touchers, QueryGeom, StartTM, Dir, DeltaMag, HitBuffer, OutputFlags, ECollisionQueryHitType::Touch, bAnyHit);
	}

	FinalizeQuery(HitBuffer);
#endif
}

void ProcessOverlaps(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& Evolution, const TArray<FPreFilterInfo>& PreFilters, const FTransform& WorldTM, const Chaos::TImplicitObject<float,3>& QueryGeom, const float Thickness, FPhysicsHitCallback<FOverlapHit>& HitBuffer, ECollisionQueryHitType HitType, bool bAnyHit)
{
	ensure(bAnyHit || HitType == ECollisionQueryHitType::Touch);	//overlaps only make sense with touch or just a single result

	using namespace Chaos;
	const TPBDRigidParticles<float, 3>& Particles = Evolution.GetParticles();
	for (const FPreFilterInfo& PreFilter : PreFilters)
	{
		const TRigidTransform<float, 3> GeomWorldTM(Particles.X(PreFilter.ActorIdx), Particles.R(PreFilter.ActorIdx));
		const TRigidTransform<float, 3> QueryToGeomTM = GeomWorldTM.GetRelativeTransform(WorldTM);

		FOverlapHit Hit;
		
		if (OverlapQuery<float,3>(*PreFilter.Geom, GeomWorldTM, QueryGeom, WorldTM, Thickness))
		{
			Hit.ActorIdx = PreFilter.ActorIdx;
			Hit.Shape = PreFilter.Geom;
			InsertOverlap(HitBuffer, Hit);

			if (bAnyHit)
			{
				return;
			}
		}
	}
}

void FChaosSQAccelerator::ChaosOverlap(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& WorldTM,FPhysicsHitCallback<FOverlapHit>& HitBuffer, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
#if WITH_PHYSX
	using namespace Chaos;

	TArray<int32> PotentialIntersections;
	{
		const ISpatialAcceleration<float, 3>& SpatialAcceleration = Evolution.GetSpatialAcceleration();
		TBox<float, 3> WorldBounds = QueryGeom.BoundingBox().TransformedBox(WorldTM);
		PotentialIntersections = SpatialAcceleration.FindAllIntersections(WorldBounds);
		Evolution.ReleaseSpatialAcceleration();
	}

	TArray<FPreFilterInfo> Overlaps;
	const bool bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
	GeneratePrefilterInfo(Evolution, PotentialIntersections, QueryFilter, QueryFilterData, bAnyHit, QueryCallback, Overlaps, Overlaps);

	ProcessOverlaps(Evolution, Overlaps, WorldTM, QueryGeom, 0, HitBuffer, bAnyHit ? ECollisionQueryHitType::Block : ECollisionQueryHitType::Touch, bAnyHit);

	FinalizeQuery(HitBuffer);
#endif
}

#endif

#if WITH_PHYSX && !WITH_CHAOS

FPhysXSQAccelerator::FPhysXSQAccelerator()
	: Scene(nullptr)
{

}

FPhysXSQAccelerator::FPhysXSQAccelerator(physx::PxScene* InScene)
	: Scene(InScene)
{

}

void FPhysXSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsRaycastInputAdapater Inputs(Start, Dir, OutputFlags);
	Scene->raycast(Inputs.Start, Inputs.Dir, DeltaMagnitude, HitBuffer, Inputs.OutputFlags, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsSweepInputAdapater Inputs(StartTM, Dir, OutputFlags);
	Scene->sweep(QueryGeom, Inputs.StartTM, Inputs.Dir, DeltaMagnitude, HitBuffer, Inputs.OutputFlags, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsOverlapInputAdapater Inputs(GeomPose);
	Scene->overlap(QueryGeom, Inputs.GeomPose, HitBuffer, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::SetScene(physx::PxScene* InScene)
{
	Scene = InScene;
}
#endif
