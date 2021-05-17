// Copyright Epic Games, Inc. All Rights Reserved.

#include "SQAccelerator.h"
#include "CollisionQueryFilterCallbackCore.h"
#include "PhysicsCore/Public/PhysicsInterfaceUtilsCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "SceneQueryPhysXImp.h"	//todo: use nice platform wrapper
#include "PhysXInterfaceWrapperCore.h"
#endif

#if WITH_CHAOS
#include "SceneQueryChaosImp.h"
#endif

#include "ChaosInterfaceWrapperCore.h"
#include "Chaos/CastingUtilities.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/DebugDrawQueue.h"

#if CHAOS_DEBUG_DRAW
int32 ChaosSQDrawDebugVisitorQueries = 0;
FAutoConsoleVariableRef CVarChaosSQDrawDebugQueries(TEXT("p.Chaos.SQ.DrawDebugVisitorQueries"), ChaosSQDrawDebugVisitorQueries, TEXT("Draw bounds of objects visited by visitors in scene queries."));
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

FChaosSQAccelerator::FChaosSQAccelerator(const Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle, float, 3>& InSpatialAcceleration)
	: SpatialAcceleration(InSpatialAcceleration)
{}

struct FPreFilterInfo
{
	const Chaos::FImplicitObject* Geom;
	int32 ActorIdx;
};

void FillHitHelper(ChaosInterface::FLocationHit& Hit, const float Distance, const FVector& WorldPosition, const FVector& WorldNormal, int32 FaceIdx, bool bComputeMTD)
{
	Hit.Distance = Distance;
	Hit.WorldPosition = WorldPosition;
	Hit.WorldNormal = WorldNormal;
	Hit.Flags = Distance > 0.f || bComputeMTD ? EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position : EHitFlags::Distance | EHitFlags::FaceIndex;
	Hit.FaceIndex = FaceIdx;
}

void FillHitHelper(ChaosInterface::FOverlapHit& Hit, const float Distance, const FVector& WorldPosition, const FVector& WorldNormal, int32 FaceIdx, bool bComputeMTD)
{
}

template <typename QueryGeometryType, typename TPayload, typename THitType>
struct TSQVisitor : public Chaos::ISpatialVisitor<TPayload, float>
{
	TSQVisitor(const FVector& InStartPoint, const FVector& InDir, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& InHitBuffer, EHitFlags InOutputFlags,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const FQueryDebugParams& InDebugParams)
		: StartPoint(InStartPoint)
		, Dir(InDir)
		, HalfExtents(0)
		, OutputFlags(InOutputFlags)
		, bAnyHit(false)
		, DebugParams(InDebugParams)
		, HitBuffer(InHitBuffer)
		, QueryFilterData(InQueryFilterData)
#if PHYSICS_INTERFACE_PHYSX
		, QueryFilterDataConcrete(P2UFilterData(QueryFilterData.data))
#else
		, QueryFilterDataConcrete(C2UFilterData(QueryFilterData.data))
#endif
		, QueryCallback(InQueryCallback)
	{
#if PHYSICS_INTERFACE_PHYSX
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#else
		bAnyHit = QueryFilterData.flags & FChaosQueryFlag::eANY_HIT;
#endif
	}

	TSQVisitor(const FTransform& InStartTM, const FVector& InDir, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& InHitBuffer, EHitFlags InOutputFlags,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const QueryGeometryType& InQueryGeom, const FQueryDebugParams& InDebugParams)
		: Dir(InDir)
		, HalfExtents(InQueryGeom.BoundingBox().Extents() * 0.5)
		, OutputFlags(InOutputFlags)
		, bAnyHit(false)
		, DebugParams(InDebugParams)
		, HitBuffer(InHitBuffer)
		, QueryFilterData(InQueryFilterData)
#if PHYSICS_INTERFACE_PHYSX
		, QueryFilterDataConcrete(P2UFilterData(QueryFilterData.data))
#else
		, QueryFilterDataConcrete(C2UFilterData(QueryFilterData.data))
#endif
		, QueryGeom(&InQueryGeom)
		, QueryCallback(InQueryCallback)
		, StartTM(InStartTM)
	{
#if PHYSICS_INTERFACE_PHYSX
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#else
		bAnyHit = QueryFilterData.flags & FChaosQueryFlag::eANY_HIT;
#endif

		//todo: check THitType is sweep
	}

	TSQVisitor(const FTransform& InWorldTM, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& InHitBuffer,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const QueryGeometryType& InQueryGeom, const FQueryDebugParams& InDebugParams)
		: HalfExtents(InQueryGeom.BoundingBox().Extents() * 0.5)
		, bAnyHit(false)
		, DebugParams(InDebugParams)
		, HitBuffer(InHitBuffer)
		, QueryFilterData(InQueryFilterData)
#if PHYSICS_INTERFACE_PHYSX
		, QueryFilterDataConcrete(P2UFilterData(QueryFilterData.data))
#else
		, QueryFilterDataConcrete(C2UFilterData(QueryFilterData.data))
#endif
		, QueryGeom(&InQueryGeom)
		, QueryCallback(InQueryCallback)
		, StartTM(InWorldTM)
	{
#if PHYSICS_INTERFACE_PHYSX
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#else
		bAnyHit = QueryFilterData.flags & FChaosQueryFlag::eANY_HIT;
#endif

		//todo: check THitType is overlap
	}

	virtual bool Raycast(const Chaos::TSpatialVisitorData<TPayload>& Instance, Chaos::FQueryFastData& CurData) override
	{
		return Visit<ESQType::Raycast>(Instance, &CurData);
	}

	virtual bool Sweep(const Chaos::TSpatialVisitorData<TPayload>& Instance, Chaos::FQueryFastData& CurData) override
	{
		return Visit<ESQType::Sweep>(Instance, &CurData);
	}

	virtual bool Overlap(const Chaos::TSpatialVisitorData<TPayload>& Instance) override
	{
		return Visit<ESQType::Overlap>(Instance, nullptr);
	}

	virtual const void* GetQueryData() const override
	{
		return &QueryFilterData;
	}

private:

	enum class ESQType
	{
		Raycast,
		Sweep,
		Overlap
	};

	template <ESQType SQ>
	bool Visit(const Chaos::TSpatialVisitorData<TPayload>& Instance, Chaos::FQueryFastData* CurData)
	{
		//QUICK_SCOPE_CYCLE_COUNTER(SQVisit);
		TPayload Payload = Instance.Payload;

		//todo: add a check to ensure hitbuffer matches SQ type
		using namespace Chaos;
		FGeometryParticle* GeometryParticle = Payload.GetExternalGeometryParticle_ExternalThread();

		if(!GeometryParticle)
		{
			// This case handles particles created by the physics simulation without the main thread
			// being made aware of their creation. We have a PT particle but no external particle
			return true;
		}

		const FShapesArray& Shapes = GeometryParticle->ShapesArray();

		const bool bTestShapeBounds =  Shapes.Num() > 1;
		bool bContinue = true;

		const TRigidTransform<float, 3> ActorTM(GeometryParticle->X(), GeometryParticle->R());
		const TAABB<FReal, 3> QueryGeomWorldBounds = QueryGeom ? QueryGeom->BoundingBox().TransformedAABB(StartTM) : TAABB<FReal, 3>(-HalfExtents, HalfExtents);

#if CHAOS_DEBUG_DRAW
		bool bAllShapesIgnoredInPrefilter = true;
		bool bHitBufferIncreased = false;
#endif

		for (const auto& Shape : Shapes)
		{
			const FImplicitObject* Geom = Shape->GetGeometry().Get();

			if (bTestShapeBounds)
			{
				FAABB3 InflatedWorldBounds;
				if (SQ == ESQType::Raycast)
				{
					InflatedWorldBounds = Shape->GetWorldSpaceInflatedShapeBounds();
				}
				else
				{
					// Transform to world bounds and get the proper half extent.
					const FVec3 WorldHalfExtent = QueryGeom ? QueryGeomWorldBounds.Extents() * 0.5f : HalfExtents;

					InflatedWorldBounds = FAABB3(Shape->GetWorldSpaceInflatedShapeBounds().Min() - WorldHalfExtent, Shape->GetWorldSpaceInflatedShapeBounds().Max() + WorldHalfExtent);
				}
				if (SQ != ESQType::Overlap)
				{
					//todo: use fast raycast
					float TmpTime;
					FVec3 TmpPos;
					if (!InflatedWorldBounds.RaycastFast( SQ == ESQType::Raycast ? StartPoint : StartTM.GetLocation(), CurData->Dir, CurData->InvDir, CurData->bParallel, CurData->CurrentLength, CurData->InvCurrentLength, TmpTime, TmpPos))
					{
						continue;
					}
				}
				else
				{
					const FVec3 QueryCenter = QueryGeom ? QueryGeomWorldBounds.Center() : StartTM.GetLocation();
					if (!InflatedWorldBounds.Contains(QueryCenter))
					{
						continue;
					}
				}
			}

			//TODO: use gt particles directly
#if PHYSICS_INTERFACE_PHYSX
			ECollisionQueryHitType HitType = QueryFilterData.flags & PxQueryFlag::ePREFILTER ? QueryCallback.PreFilter(QueryFilterDataConcrete, *Shape, *GeometryParticle) : ECollisionQueryHitType::Block;
#else
			ECollisionQueryHitType HitType = QueryFilterData.flags & FChaosQueryFlag::ePREFILTER ? QueryCallback.PreFilter(QueryFilterDataConcrete, *Shape, *GeometryParticle) : ECollisionQueryHitType::Block;
#endif


			if (HitType != ECollisionQueryHitType::None)
			{
#if CHAOS_DEBUG_DRAW
				bAllShapesIgnoredInPrefilter = false;
#endif

				//QUICK_SCOPE_CYCLE_COUNTER(SQNarrow);
				THitType Hit;
				Hit.Actor = GeometryParticle;
				Hit.Shape = Shape.Get();

				bool bHit = false;

				FVec3 WorldPosition, WorldNormal;
				float Distance = 0;	//not needed but fixes compiler warning for overlap
				int32 FaceIdx = INDEX_NONE;	//not needed but fixes compiler warning for overlap
				const bool bComputeMTD = !!((uint16)(OutputFlags.HitFlags & EHitFlags::MTD));

				if (SQ == ESQType::Raycast)
				{
					FVec3 LocalNormal;
					FVec3 LocalPosition;

					const FVec3 DirLocal = ActorTM.InverseTransformVectorNoScale(Dir);
					const FVec3 StartLocal = ActorTM.InverseTransformPositionNoScale(StartPoint);
					bHit = Geom->Raycast(StartLocal, DirLocal, CurData->CurrentLength, /*Thickness=*/0.f, Distance, LocalPosition, LocalNormal, FaceIdx);
					if (bHit)
					{
						WorldPosition = ActorTM.TransformPositionNoScale(LocalPosition);
						WorldNormal = ActorTM.TransformVectorNoScale(LocalNormal);
					}
				}
				else if(SQ == ESQType::Sweep && CurData->CurrentLength > 0 && ensure(QueryGeom))
				{
					bHit = SweepQuery(*Geom, ActorTM, *QueryGeom, StartTM, CurData->Dir, CurData->CurrentLength, Distance, WorldPosition, WorldNormal, FaceIdx, 0.f, bComputeMTD);
				}
				else if ((SQ == ESQType::Overlap || (SQ == ESQType::Sweep && CurData->CurrentLength == 0)) && ensure(QueryGeom))
				{
					if (bComputeMTD)
					{
						FMTDInfo MTDInfo;
						bHit = OverlapQuery(*Geom, ActorTM, *QueryGeom, StartTM, /*Thickness=*/0, &MTDInfo);
						if (bHit)
						{
							WorldNormal = MTDInfo.Normal * MTDInfo.Penetration;
						}
					}
					else
					{
						bHit = OverlapQuery(*Geom, ActorTM, *QueryGeom, StartTM, /*Thickness=*/0);
					}
				}

				if(bHit)
				{
					//QUICK_SCOPE_CYCLE_COUNTER(SQNarrowHit);
					FillHitHelper(Hit, Distance, WorldPosition, WorldNormal, FaceIdx, bComputeMTD);


#if PHYSICS_INTERFACE_PHYSX
					HitType = QueryFilterData.flags & PxQueryFlag::ePOSTFILTER ? QueryCallback.PostFilter(QueryFilterDataConcrete, Hit) : HitType;
#else
					HitType = QueryFilterData.flags & FChaosQueryFlag::ePOSTFILTER ? QueryCallback.PostFilter(QueryFilterDataConcrete, Hit) : HitType;
#endif

					if (HitType != ECollisionQueryHitType::None)
					{

						//overlap never blocks
						const bool bBlocker = (HitType == ECollisionQueryHitType::Block || bAnyHit || HitBuffer.WantsSingleResult());
						HitBuffer.InsertHit(Hit, bBlocker);
#if CHAOS_DEBUG_DRAW
						bHitBufferIncreased = true;
#endif

						if (bBlocker && SQ != ESQType::Overlap)
						{
							CurData->SetLength(FMath::Max(0.f, Distance));	//Max is needed for MTD which returns negative distance
							if (CurData->CurrentLength == 0 && (SQ == ESQType::Raycast || HitBuffer.WantsSingleResult()))	//raycasts always fail with distance 0, sweeps only matter if we want multi overlaps
							{
								bContinue = false; //initial overlap so nothing will be better than this
								break;
							}
						}

						if (bAnyHit)
						{
							bContinue = false;
							break;
						}
					}
				}
			}
		}

#if CHAOS_DEBUG_DRAW && WITH_CHAOS
		if (DebugParams.IsDebugQuery() && ChaosSQDrawDebugVisitorQueries)
		{
			DebugDraw<SQ>(Instance, CurData, bAllShapesIgnoredInPrefilter, bHitBufferIncreased);
		}
#endif

		return bContinue;
	}

#if CHAOS_DEBUG_DRAW

	void DebugDrawPayloadImpl(const TPayload& Payload, const bool bExternal, const bool bHit, decltype(&TPayload::DebugDraw)) { Payload.DebugDraw(bExternal, bHit); }
	void DebugDrawPayloadImpl(const TPayload& Payload, const bool bExternal, const bool bHit, ...) { }
	void DebugDrawPayload(const TPayload& Payload, const bool bExternal, const bool bHit) { DebugDrawPayloadImpl(Payload, bExternal, bHit, 0); }

	template <ESQType SQ>
	void DebugDraw(const Chaos::TSpatialVisitorData<TPayload>& Instance, const Chaos::FQueryFastData* CurData, const bool bPrefiltered, const bool bHit)
	{
		if (SQ == ESQType::Raycast)
		{
			const FVector EndPoint = StartPoint + (Dir * CurData->CurrentLength);
			Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(StartPoint, EndPoint, 5.f, bHit ? FColor::Red : FColor::Green);
		}
		else if (SQ == ESQType::Overlap)
		{
			Chaos::DebugDraw::DrawShape(StartTM, QueryGeom, bHit ? FColor::Red : FColor::Green);
		}

		if (Instance.bHasBounds)
		{
			Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Instance.Bounds.Center(), Instance.Bounds.Extents(), FQuat::Identity, bHit ? FColor(100, 50, 50) : FColor(50, 100, 50), false, -1.f, 0, 0.f);
		}

#if WITH_CHAOS
		if (!bPrefiltered)
		{
			DebugDrawPayload(Instance.Payload, DebugParams.bExternalQuery, bHit);
		}
#endif
	}
#endif

	const FVector StartPoint;
	const FVector Dir;
	const FVector HalfExtents;
	FHitFlags OutputFlags;
	bool bAnyHit;
	const FQueryDebugParams DebugParams;
	ChaosInterface::FSQHitBuffer<THitType>& HitBuffer;
	const FQueryFilterData& QueryFilterData;
	const FCollisionFilterData QueryFilterDataConcrete;
	const QueryGeometryType* QueryGeom = nullptr;
	ICollisionQueryFilterCallbackBase& QueryCallback;
	const FTransform StartTM;
};

template <typename QueryGeometryType, typename TPayload, typename THitType>
struct TBPVisitor : public Chaos::ISpatialVisitor<TPayload, float>
{
	TBPVisitor(const FTransform& InWorldTM, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& InHitBuffer,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const QueryGeometryType& InQueryGeom, const FQueryDebugParams& InDebugParams)
		: HalfExtents(InQueryGeom.BoundingBox().Extents() * 0.5)
		, bAnyHit(false)
		, DebugParams(InDebugParams)
		, HitBuffer(InHitBuffer)
		, QueryFilterData(InQueryFilterData)
		, QueryFilterDataConcrete(ToUnrealFilterData(InQueryFilterData.data))
		, QueryGeom(&InQueryGeom)
		, QueryCallback(InQueryCallback)
		, StartTM(InWorldTM)
	{
		bAnyHit = QueryFilterData.flags & FPhysicsQueryFlag::eANY_HIT;
	}
	virtual bool Overlap(const Chaos::TSpatialVisitorData<TPayload>& Instance) override
	{
		return Visit<ESQType::Overlap>(Instance, nullptr);
	}

	virtual bool Raycast(const Chaos::TSpatialVisitorData<TPayload>& Instance, Chaos::FQueryFastData& CurData) override
	{
		ensure(false);
		return false;
	}

	virtual bool Sweep(const Chaos::TSpatialVisitorData<TPayload>& Instance, Chaos::FQueryFastData& CurData) override
	{
		ensure(false);
		return false;
	}

	virtual const void* GetQueryData() const override
	{
		return &QueryFilterData;
	}

private:

	enum class ESQType
	{
		Raycast,
		Sweep,
		Overlap
	};

	template <ESQType SQ>
	bool Visit(const Chaos::TSpatialVisitorData<TPayload>& Instance, Chaos::FQueryFastData* CurData)
	{
		bool bContinue = true;
		TPayload Payload = Instance.Payload;
		//todo: add a check to ensure hitbuffer matches SQ type
		using namespace Chaos;
		FGeometryParticle* GeometryParticle = Payload.GetExternalGeometryParticle_ExternalThread();
		if (!GeometryParticle)
		{
			// This case handles particles created by the physics simulation without the main thread
			// being made aware of their creation. We have a PT particle but no external particle
			return true;
		}
		const FShapesArray& Shapes = GeometryParticle->ShapesArray();
		THitType Hit;
		Hit.Actor = GeometryParticle;
		for (const auto& Shape : Shapes)
		{
			ECollisionQueryHitType HitType = QueryFilterData.flags & FPhysicsQueryFlag::ePREFILTER ? QueryCallback.PreFilter(QueryFilterDataConcrete, *Shape, *GeometryParticle) : ECollisionQueryHitType::Block;
			if (HitType != ECollisionQueryHitType::None)
			{
				const bool bBlocker = (HitType == ECollisionQueryHitType::Block || bAnyHit || HitBuffer.WantsSingleResult());
				Hit.Shape = Shape.Get();
				HitBuffer.InsertHit(Hit, bBlocker);
				if (bAnyHit)
				{
					bContinue = false;
				}
				break;
			}
		}
		return bContinue;
	}

	const FVector StartPoint;
	const FVector Dir;
	const FVector HalfExtents;
	FHitFlags OutputFlags;
	bool bAnyHit;
	const FQueryDebugParams DebugParams;
	ChaosInterface::FSQHitBuffer<THitType>& HitBuffer;
	const FQueryFilterData& QueryFilterData;
	const FCollisionFilterData QueryFilterDataConcrete;
	const QueryGeometryType* QueryGeom;
	ICollisionQueryFilterCallbackBase& QueryCallback;
	const FTransform StartTM;
};

void FChaosSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	TSQVisitor<TSphere<FReal,3>, FAccelerationStructureHandle, FRaycastHit> RaycastVisitor(Start, Dir, HitBuffer, OutputFlags, QueryFilterData, QueryCallback, DebugParams);
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Raycast(Start, Dir, DeltaMagnitude, RaycastVisitor);
	HitBuffer.DecFlushCount();
}

template <typename QueryGeomType>
void SweepHelper(const QueryGeomType& QueryGeom,const Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle,float,3>& SpatialAcceleration,const FTransform& StartTM,const FVector& Dir,const float DeltaMagnitude,ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer,EHitFlags OutputFlags,const FQueryFilterData& QueryFilterData,ICollisionQueryFilterCallbackBase& QueryCallback,const FQueryDebugParams& DebugParams)
{
	using namespace Chaos;
	using namespace ChaosInterface;

	const FAABB3 Bounds = QueryGeom.BoundingBox().TransformedAABB(StartTM);
	const bool bSweepAsOverlap = DeltaMagnitude == 0;	//question: do we care about tiny sweeps?
	TSQVisitor<QueryGeomType,FAccelerationStructureHandle,FSweepHit> SweepVisitor(StartTM,Dir,HitBuffer,OutputFlags,QueryFilterData,QueryCallback,QueryGeom,DebugParams);

	HitBuffer.IncFlushCount();

	if(bSweepAsOverlap)
	{
		//fallback to overlap
		SpatialAcceleration.Overlap(Bounds,SweepVisitor);
	} else
	{
		const FVector HalfExtents = Bounds.Extents() * 0.5f;
		SpatialAcceleration.Sweep(Bounds.GetCenter(),Dir,DeltaMagnitude,HalfExtents,SweepVisitor);
	}

	HitBuffer.DecFlushCount();
}

void FChaosSQAccelerator::Sweep(const Chaos::FImplicitObject& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams) const
{
	return Chaos::Utilities::CastHelper(QueryGeom, StartTM, [&](const auto& Downcast, const FTransform& StartFullTM) { return SweepHelper(Downcast, SpatialAcceleration, StartFullTM, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFilterData, QueryCallback, DebugParams); });
}

template <typename QueryGeomType>
void OverlapHelper(const QueryGeomType& QueryGeom, const Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle, float, 3>& SpatialAcceleration, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams)
{
	using namespace Chaos;
	using namespace ChaosInterface;

	const FAABB3 Bounds = QueryGeom.BoundingBox().TransformedAABB(GeomPose);

	HitBuffer.IncFlushCount();
#if PHYSICS_INTERFACE_PHYSX
	bool bSkipNarrowPhase = false; // Flag doesn't exist on PhysX type.
#else
	bool bSkipNarrowPhase = QueryFilterData.flags & FPhysicsQueryFlag::eSKIPNARROWPHASE;
#endif
	if (bSkipNarrowPhase)
	{
		TBPVisitor<QueryGeomType, FAccelerationStructureHandle, FOverlapHit> OverlapVisitor(GeomPose, HitBuffer, QueryFilterData, QueryCallback, QueryGeom, DebugParams);
		SpatialAcceleration.Overlap(Bounds, OverlapVisitor);
	}
	else
	{
		TSQVisitor<QueryGeomType, FAccelerationStructureHandle, FOverlapHit> OverlapVisitor(GeomPose, HitBuffer, QueryFilterData, QueryCallback, QueryGeom, DebugParams);
		SpatialAcceleration.Overlap(Bounds, OverlapVisitor);
	}
	HitBuffer.DecFlushCount();
}

void FChaosSQAccelerator::Overlap(const Chaos::FImplicitObject& QueryGeom, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams) const
{
	return Chaos::Utilities::CastHelper(QueryGeom, GeomPose, [&](const auto& Downcast, const FTransform& GeomFullPose) { return OverlapHelper(Downcast, SpatialAcceleration, GeomFullPose, HitBuffer, QueryFilterData, QueryCallback, DebugParams); });
}

#if WITH_PHYSX
FChaosSQAcceleratorAdapter::FChaosSQAcceleratorAdapter(const Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle, float, 3>& InSpatialAcceleration)
	: ChaosSQAccelerator(InSpatialAcceleration)
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
