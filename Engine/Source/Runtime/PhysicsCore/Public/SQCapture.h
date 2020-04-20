// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsInterfaceDeclaresCore.h"
#include "CoreMinimal.h"
#include "ChaosSQTypes.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "PhysXPublicCore.h"
#include "PhysicsInterfaceDeclaresCore.h"
#include "PhysXInterfaceWrapperCore.h"
#include "CollisionQueryFilterCallbackCore.h"
#include "ChaosInterfaceWrapperCore.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"

class FPhysTestSerializer;

namespace ChaosInterface
{
	struct FSweepHit;
	struct FRaycastHit;
	struct FOverlapHit;
}

namespace Chaos
{
	class FChaosArchive;
}

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

#if PHYSICS_INTERFACE_PHYSX
	void StartCapturePhysXSweep(const PxScene& Scene, const PxGeometry& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCapturePhysXSweep(const PxHitCallback<PxSweepHit>& Results);

	void StartCapturePhysXRaycast(const PxScene& Scene, const FVector& InStartPoint, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCapturePhysXRaycast(const PxHitCallback<PxRaycastHit>& Results);

	void StartCapturePhysXOverlap(const PxScene& Scene, const PxGeometry& InQueryGeom, const FTransform& WorldTM, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCapturePhysXOverlap(const PxHitCallback<PxOverlapHit>& Results);
#endif

	void StartCaptureChaosSweep(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FImplicitObject& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCaptureChaosSweep(const ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& Results);

	void StartCaptureChaosRaycast(const Chaos::FPBDRigidsEvolution& Evolution, const FVector& InStartPoint, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCaptureChaosRaycast(const ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& Results);

	void StartCaptureChaosOverlap(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FImplicitObject& InQueryGeom, const FTransform& InStartTM, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCaptureChaosOverlap(const ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& Results);

	ECollisionQueryHitType GetFilterResult(const Chaos::FPerShapeData* Shape, const Chaos::TGeometryParticle<float,3>* Actor) const;

#if PHYSICS_INTERFACE_PHYSX
	ECollisionQueryHitType GetFilterResult(const physx::PxShape* Shape, const physx::PxActor* Actor) const;
#endif
	
	FVector Dir;
	FTransform StartTM;	//only valid if overlap or sweep
	FVector StartPoint;	//only valid if raycast

	float DeltaMag;
	FHitFlags OutputFlags;
	FQueryFilterData QueryFilterData;
	TUniquePtr<ICollisionQueryFilterCallbackBase> FilterCallback;

#if PHYSICS_INTERFACE_PHYSX
	PhysXInterface::FDynamicHitBuffer<PxSweepHit> PhysXSweepBuffer;
	PhysXInterface::FDynamicHitBuffer<PxRaycastHit> PhysXRaycastBuffer;
	PhysXInterface::FDynamicHitBuffer<PxOverlapHit> PhysXOverlapBuffer;
	PxGeometryHolder PhysXGeometry;
#endif

	TUniquePtr<Chaos::FImplicitObject> ChaosOwnerObject;	//should be private, do not access directly
	const Chaos::FImplicitObject* ChaosGeometry;
	TUniquePtr<Chaos::FImplicitObject> SerializableChaosGeometry;
#if WITH_CHAOS
	//for now just use physx hit buffer
	ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit> ChaosSweepBuffer;
	TArray<ChaosInterface::FSweepHit> ChaosSweepTouches;

	ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit> ChaosRaycastBuffer;
	TArray<ChaosInterface::FRaycastHit> ChaosRaycastTouches;

	ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit> ChaosOverlapBuffer;
	TArray<ChaosInterface::FOverlapHit> ChaosOverlapTouches;
#endif

private:
	FSQCapture(FPhysTestSerializer& OwningPhysSerializer);	//This should be created by PhysTestSerializer
	void Serialize(Chaos::FChaosArchive& Ar);

#if PHYSICS_INTERFACE_PHYSX
	void SerializeActorToShapeHitsArray(FArchive& Ar);
#endif

	friend FPhysTestSerializer;

	TArray<uint8> GeomData;
	TArray<uint8> HitData;

#if 0
	void CreateChaosDataFromPhysX();
	void CreateChaosFilterResults();
#endif

#if PHYSICS_INTERFACE_PHYSX

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
	void SerializePhysXBuffers(FArchive& Ar, int32 Version, PhysXInterface::FDynamicHitBuffer<THit>& PhysXBuffer);

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

	TMap<physx::PxActor*, TArray<TPair<physx::PxShape*, ECollisionQueryHitType>>> PxActorToShapeHitsArray;

	//only valid during capture when serializing out runtime structures that already use non transient data
	TMap<physx::PxActor*, physx::PxActor*> NonTransientToTransientActors;
	TMap<physx::PxShape*, physx::PxShape*> NonTransientToTransientShapes;
#endif

	FPhysTestSerializer& PhysSerializer;

	void CaptureChaosFilterResults(const Chaos::FPBDRigidsEvolution& Evolution, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);

	TMap<Chaos::TGeometryParticle<float,3>*, TArray<TPair<Chaos::FPerShapeData*, ECollisionQueryHitType>>> ChaosActorToShapeHitsArray;

	template <typename THit>
	void SerializeChaosBuffers(Chaos::FChaosArchive& Ar, int32 Version, ChaosInterface::FSQHitBuffer<THit>& ChaosBuffer);

	void SerializeChaosActorToShapeHitsArray(Chaos::FChaosArchive& Ar);

	bool bDiskDataIsChaos;
	bool bChaosDataReady;
	bool bPhysXDataReady;
};
