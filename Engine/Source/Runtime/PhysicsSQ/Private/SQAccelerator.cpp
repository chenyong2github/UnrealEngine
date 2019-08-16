// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SQAccelerator.h"
#include "CollisionQueryFilterCallbackCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "SceneQueryPhysXImp.h"	//todo: use nice platform wrapper
#include "PhysXInterfaceWrapperCore.h"
#endif

#if WITH_CHAOS
#include "Experimental/SceneQueryChaosImp.h"
#endif

#if INCLUDE_CHAOS
#include "ChaosInterfaceWrapperCore.h"

#include "Chaos/Particles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/GeometryQueries.h"
#endif

void FSQAcceleratorUnion::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Raycast(Start, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Sweep(QueryGeom, StartTM, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Overlap(QueryGeom, GeomPose, HitBuffer, QueryFilterData, QueryCallback);
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

void FillHitHelper(ChaosInterface::FLocationHit& Hit, const float Distance, const FVector& WorldPosition, const FVector& WorldNormal)
{
	Hit.Distance = Distance;
	Hit.WorldPosition = WorldPosition;
	Hit.WorldNormal = WorldNormal;
	Hit.Flags = Distance > 0 ? EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position : EHitFlags::Distance;
}

void FillHitHelper(ChaosInterface::FOverlapHit& Hit, const float Distance, const FVector& WorldPosition, const FVector& WorldNormal)
{
}

template <typename TPayload, typename THitType>
struct TSQVisitor : public Chaos::ISpatialVisitor<TPayload, float>
{
	TSQVisitor(const FVector& InStartPoint, const FVector& InDir, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& InHitBuffer, EHitFlags InOutputFlags,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback)
		: StartPoint(InStartPoint)
		, Dir(InDir)
		, HitBuffer(InHitBuffer)
		, OutputFlags(InOutputFlags)
		, QueryFilterData(InQueryFilterData)
		, QueryCallback(InQueryCallback)
		, bAnyHit(false)
	{
#if WITH_PHYSX
		//#TODO - reimplement query flags alternative for Chaos
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#endif
	}

	TSQVisitor(const FTransform& InStartTM, const FVector& InDir, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& InHitBuffer, EHitFlags InOutputFlags,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const Chaos::TImplicitObject<float,3>& InQueryGeom)
		: StartTM(InStartTM)
		, Dir(InDir)
		, HitBuffer(InHitBuffer)
		, OutputFlags(InOutputFlags)
		, QueryFilterData(InQueryFilterData)
		, QueryCallback(InQueryCallback)
		, bAnyHit(false)
		, QueryGeom(&InQueryGeom)
	{
#if WITH_PHYSX
		//#TODO - reimplement query flags alternative for Chaos
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#endif
		//todo: check THitType is sweep
	}

	TSQVisitor(const FTransform& InWorldTM, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& InHitBuffer,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const Chaos::TImplicitObject<float, 3>& InQueryGeom)
		: StartTM(InWorldTM)
		, HitBuffer(InHitBuffer)
		, QueryFilterData(InQueryFilterData)
		, QueryCallback(InQueryCallback)
		, bAnyHit(false)
		, QueryGeom(&InQueryGeom)
	{
#if WITH_PHYSX
		//#TODO - reimplement query flags alternative for Chaos
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#endif
		//todo: check THitType is overlap
	}

	virtual bool Raycast(const TPayload Payload, float& CurLength) override
	{
		return Visit<ESQType::Raycast>(Payload, CurLength);
	}

	virtual bool Sweep(const TPayload Payload, float& CurLength) override
	{
		return Visit<ESQType::Sweep>(Payload, CurLength);
	}

	virtual bool Overlap(const TPayload Payload) override
	{
		float DummyLength;
		return Visit<ESQType::Overlap>(Payload, DummyLength);
	}

private:

	enum class ESQType
	{
		Raycast,
		Sweep,
		Overlap
	};

	template <ESQType SQ>
	bool Visit(const TPayload Payload, float& CurLength)
	{
		//todo: add a check to ensure hitbuffer matches SQ type
		using namespace Chaos;
		const TShapesArray<float,3>& Shapes = Payload->GTGeometryParticle()->ShapesArray();	//TODO: use GT particles directly

		for (const auto& Shape : Shapes)
		{
			const TImplicitObject<float, 3>* Geom = Shape->Geometry;
			//TODO: use gt particles directly
			//#TODO alternative to px flags
#if WITH_PHYSX
			ECollisionQueryHitType HitType = QueryFilterData.flags & PxQueryFlag::ePREFILTER ? QueryCallback.PreFilter(P2UFilterData(QueryFilterData.data), *Shape, *Payload->GTGeometryParticle()) : ECollisionQueryHitType::Block;
#else
			//#TODO Chaos flag alternative
			ensure(false);
			ECollisionQueryHitType HitType = ECollisionQueryHitType::Block;
#endif
			if (HitType != ECollisionQueryHitType::None)
			{
				const TRigidTransform<float, 3> ActorTM(Payload->X(), Payload->R());

				THitType Hit;
				Hit.Actor = Payload->GTGeometryParticle();	//todo: use GT particles directly
				Hit.Shape = Shape.Get();

				bool bHit = false;

				TVector<float, 3> WorldPosition, WorldNormal;
				float Distance = 0;	//not needed but fixes compiler warning for overlap
				int32 FaceIdx;	//todo: pass back to unreal

				if (SQ == ESQType::Raycast)
				{
					TVector<float, 3> LocalNormal;
					TVector<float, 3> LocalPosition;

					const TVector<float, 3> DirLocal = ActorTM.InverseTransformVectorNoScale(Dir);
					const TVector<float, 3> StartLocal = ActorTM.InverseTransformPositionNoScale(StartPoint);
					bHit = Geom->Raycast(StartLocal, DirLocal, CurLength, /*Thickness=*/0.f, Distance, LocalPosition, LocalNormal, FaceIdx);
					if (bHit)
					{
						WorldPosition = ActorTM.TransformPositionNoScale(LocalPosition);
						WorldNormal = ActorTM.TransformVectorNoScale(LocalNormal);
					}
				}
				else if(SQ == ESQType::Sweep && CurLength > 0)
				{
					bHit = SweepQuery<float, 3>(*Geom, ActorTM, *QueryGeom, StartTM, Dir, CurLength, Distance, WorldPosition, WorldNormal, FaceIdx);
				}
				else if (SQ == ESQType::Overlap || (SQ == ESQType::Sweep && CurLength == 0))
				{
					bHit = OverlapQuery<float, 3>(*Geom, ActorTM, *QueryGeom, StartTM, /*Thickness=*/0);
				}

				if(bHit)
				{
					FillHitHelper(Hit, Distance, WorldPosition, WorldNormal);
#if WITH_PHYSX
					HitType = QueryFilterData.flags & PxQueryFlag::ePOSTFILTER ? QueryCallback.PostFilter(P2UFilterData(QueryFilterData.data), Hit) : HitType;
#else
					//#TODO Chaos flag alternative
					ensure(false);
#endif
					if (HitType != ECollisionQueryHitType::None)
					{

						//overlap never blocks
						const bool bBlocker = SQ != ESQType::Overlap && (HitType == ECollisionQueryHitType::Block || bAnyHit || HitBuffer.WantsSingleResult());
						HitBuffer.InsertHit(Hit, bBlocker);

						if (bBlocker)
						{
							CurLength = Distance;
							if (CurLength == 0 && (SQ == ESQType::Raycast || HitBuffer.WantsSingleResult()))	//raycasts always fail with distance 0, sweeps only matter if we want multi overlaps
							{
								return false;	//initial overlap so nothing will be better than this
							}
						}

						if (bAnyHit)
						{
							return false;
						}
					}
				}
			}
		}

		return true;
	}

	const FTransform StartTM;
	const FVector StartPoint;
	const FVector Dir;
	ChaosInterface::FSQHitBuffer<THitType>& HitBuffer;
	EHitFlags OutputFlags;
	const FQueryFilterData& QueryFilterData;
	ICollisionQueryFilterCallbackBase& QueryCallback;
	bool bAnyHit;
	const Chaos::TImplicitObject<float, 3>* QueryGeom;
};

void FChaosSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	TSQVisitor<TGeometryParticleHandle<float, 3>*, FRaycastHit> RaycastVisitor(Start, Dir, HitBuffer, OutputFlags, QueryFilterData, QueryCallback);
	const auto& SpatialAcceleration = Evolution.GetCollisionConstraints().GetSpatialAcceleration();
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Raycast(Start, Dir, DeltaMagnitude, RaycastVisitor);
	HitBuffer.DecFlushCount();
	Evolution.GetCollisionConstraints().ReleaseSpatialAcceleration();
}

void FChaosSQAccelerator::Sweep(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	const TBox<float, 3> Bounds = QueryGeom.BoundingBox().TransformedBox(StartTM);
	const FVector HalfExtents = Bounds.Extents() * 0.5f;
	TSQVisitor<TGeometryParticleHandle<float, 3>*, FSweepHit> SweepVisitor(StartTM, Dir, HitBuffer, OutputFlags, QueryFilterData, QueryCallback, QueryGeom);
	const auto& SpatialAcceleration = Evolution.GetCollisionConstraints().GetSpatialAcceleration();
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Sweep(Bounds.GetCenter(), Dir, DeltaMagnitude, HalfExtents, SweepVisitor);
	HitBuffer.DecFlushCount();
	Evolution.GetCollisionConstraints().ReleaseSpatialAcceleration();
}

void FChaosSQAccelerator::Overlap(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	const TBox<float, 3> Bounds = QueryGeom.BoundingBox().TransformedBox(GeomPose);
	TSQVisitor<TGeometryParticleHandle<float, 3>*, FOverlapHit> OverlapVisitor(GeomPose, HitBuffer, QueryFilterData, QueryCallback, QueryGeom);
	const auto& SpatialAcceleration = Evolution.GetCollisionConstraints().GetSpatialAcceleration();
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Overlap(Bounds, OverlapVisitor);
	HitBuffer.DecFlushCount();
	Evolution.GetCollisionConstraints().ReleaseSpatialAcceleration();
}

#endif

#if WITH_PHYSX && INCLUDE_CHAOS
FChaosSQAcceleratorAdapter::FChaosSQAcceleratorAdapter(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& InEvolution)
	: ChaosSQAccelerator(InEvolution)
{
}

void FChaosSQAcceleratorAdapter::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(false);
}

void FChaosSQAcceleratorAdapter::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(false);
}

void FChaosSQAcceleratorAdapter::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(false);
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

void FPhysXSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsRaycastInputAdapater Inputs(Start, Dir, OutputFlags);
	Scene->raycast(Inputs.Start, Inputs.Dir, DeltaMagnitude, HitBuffer, Inputs.OutputFlags, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsSweepInputAdapater Inputs(StartTM, Dir, OutputFlags);
	Scene->sweep(QueryGeom, Inputs.StartTM, Inputs.Dir, DeltaMagnitude, HitBuffer, Inputs.OutputFlags, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
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
