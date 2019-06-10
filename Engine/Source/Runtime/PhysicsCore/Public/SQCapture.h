// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsInterfaceDeclaresCore.h"
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED

#include "CoreMinimal.h"
#include "ChaosSQTypes.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "PhysXPublicCore.h"
#include "PhysicsInterfaceDeclaresCore.h"
#include "PhysXInterfaceWrapperCore.h"
#include "CollisionQueryFilterCallbackCore.h"


class FPhysTestSerializer;

//Allows us to capture a scene query with either physx or chaos and then load it into either format for testing purposes
struct PHYSICSCORE_API FSQCapture
{
	~FSQCapture();
	FSQCapture(const FSQCapture&) = delete;
	FSQCapture& operator=(const FSQCapture&) = delete;

	enum class ESQType : uint8
	{
		Raycast,
		Sweep,
		Overlap
	} SQType;

#if WITH_PHYSX
	void StartCapturePhysXSweep(const PxScene& Scene, const PxGeometry& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCapturePhysXSweep(const PxHitCallback<PxSweepHit>& Results);

	void StartCapturePhysXRaycast(const PxScene& Scene, const FVector& InStartPoint, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCapturePhysXRaycast(const PxHitCallback<PxRaycastHit>& Results);

	void StartCapturePhysXOverlap(const PxScene& Scene, const PxGeometry& InQueryGeom, const FTransform& WorldTM, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCapturePhysXOverlap(const PxHitCallback<PxOverlapHit>& Results);
#endif

#if INCLUDE_CHAOS
	void StartCaptureChaos(const Chaos::TImplicitObject<float, 3>& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, EHitFlags InOutputFlags);
	void EndCaptureChaos(const FPhysicsHitCallback<FSweepHit>& Results);
#endif

#if WITH_PHYSX
	ECollisionQueryHitType GetFilterResult(const physx::PxShape* Shape, const physx::PxActor* Actor) const;
#endif

#if INCLUDE_CHAOS
	ECollisionQueryHitType GetFilterResult(const Chaos::TImplicitObject<float,3>& Shape, const int32 ActorIdx, const int32 ShapeIdx) const;
#endif
	
	FVector Dir;
	FTransform StartTM;	//only valid if overlap or sweep
	FVector StartPoint;	//only valid if raycast

	float DeltaMag;
	FHitFlags OutputFlags;
	FQueryFilterData QueryFilterData;
	TUniquePtr<ICollisionQueryFilterCallbackBase> FilterCallback;

#if WITH_PHYSX
	FDynamicHitBuffer<PxSweepHit> PhysXSweepBuffer;
	FDynamicHitBuffer<PxRaycastHit> PhysXRaycastBuffer;
	FDynamicHitBuffer<PxOverlapHit> PhysXOverlapBuffer;
	PxGeometryHolder PhysXGeometry;
#endif

#if INCLUDE_CHAOS
	TUniquePtr<Chaos::TImplicitObject<float, 3>> ChaosOwnerObject;	//should be private, do not access directly
	const Chaos::TImplicitObject<float, 3>* ChaosGeometry;
#if WITH_PHYSX
	//for now just use physx hit buffer
	FDynamicHitBuffer<FSweepHit> ChaosSweepBuffer;
	TArray<FSweepHit> ChaosSweepTouches;

	FDynamicHitBuffer<FRaycastHit> ChaosRaycastBuffer;
	TArray<FRaycastHit> ChaosRaycastTouches;

	FDynamicHitBuffer<FOverlapHit> ChaosOverlapBuffer;
	TArray<FOverlapHit> ChaosOverlapTouches;
#endif
#endif

private:
	FSQCapture(FPhysTestSerializer& OwningPhysSerializer);	//This should be created by PhysTestSerializer
	void Serialize(FArchive& Ar);

#if WITH_PHYSX
	void SerializeActorToShapeHitsArray(FArchive& Ar);
#endif

	friend FPhysTestSerializer;

	TArray<uint8> GeomData;
	TArray<uint8> HitData;

#if INCLUDE_CHAOS
	void CreateChaosData();
	void CreateChaosFilterResults();
#endif

#if WITH_PHYSX

	static constexpr uint64 ShapeCollectionID = 1;

	void CapturePhysXFilterResults(const PxScene& Scene, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void CreatePhysXData();
	void SetPhysXGeometryData(const PxGeometry& Geometry);

	physx::PxActor* GetTransientActor(physx::PxActor* Actor) const;

	physx::PxShape* GetTransientShape(physx::PxShape* Shape) const;

	void SerializePhysXHitType(FArchive& Ar, PxOverlapHit& Hit);

	template <typename T>
	void SerializePhysXHitType(FArchive& Ar, T& Hit);

	template <typename THit>
	void SerializePhysXBuffers(FArchive& Ar, int32 Version, FDynamicHitBuffer<THit>& PhysXBuffer);

	struct FPhysXSerializerData
	{
		FPhysXSerializerData(int32 NumBytes);
		~FPhysXSerializerData();

		void* Data;
		physx::PxShape* Shape;	//holder for geometry so we can serialize it out in a collection
		physx::PxCollection* Collection;
		physx::PxSerializationRegistry* Registry;
	};
	TUniquePtr<FPhysXSerializerData> AlignedDataHelper;

	TMap<physx::PxActor*, TArray<TPair<physx::PxShape*, ECollisionQueryHitType>>> ActorToShapeHitsArray;

	//only valid during capture when serializing out runtime structures that already use non transient data
	TMap<physx::PxActor*, physx::PxActor*> NonTransientToTransientActors;
	TMap<physx::PxShape*, physx::PxShape*> NonTransientToTransientShapes;
#endif

	FPhysTestSerializer& PhysSerializer;

#if INCLUDE_CHAOS
	TMap<int32, TArray<ECollisionQueryHitType>> ChaosActorToHitsArray;
#endif
	bool bDiskDataIsChaos;
	bool bChaosDataReady;
	bool bPhysXDataReady;
};

#endif