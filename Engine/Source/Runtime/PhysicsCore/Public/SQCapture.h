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

	void StartCaptureChaosSweep(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FImplicitObject& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCaptureChaosSweep(const ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& Results);

	void StartCaptureChaosRaycast(const Chaos::FPBDRigidsEvolution& Evolution, const FVector& InStartPoint, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCaptureChaosRaycast(const ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& Results);

	void StartCaptureChaosOverlap(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FImplicitObject& InQueryGeom, const FTransform& InStartTM, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);
	void EndCaptureChaosOverlap(const ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& Results);

	ECollisionQueryHitType GetFilterResult(const Chaos::FPerShapeData* Shape, const Chaos::FGeometryParticle* Actor) const;
	
	FVector Dir;
	FTransform StartTM;	//only valid if overlap or sweep
	FVector StartPoint;	//only valid if raycast

	float DeltaMag;
	FHitFlags OutputFlags;
	FQueryFilterData QueryFilterData;
	TUniquePtr<ICollisionQueryFilterCallbackBase> FilterCallback;

	TUniquePtr<Chaos::FImplicitObject> ChaosOwnerObject;	//should be private, do not access directly
	const Chaos::FImplicitObject* ChaosGeometry;
	TUniquePtr<Chaos::FImplicitObject> SerializableChaosGeometry;

	ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit> ChaosSweepBuffer;
	TArray<ChaosInterface::FSweepHit> ChaosSweepTouches;

	ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit> ChaosRaycastBuffer;
	TArray<ChaosInterface::FRaycastHit> ChaosRaycastTouches;

	ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit> ChaosOverlapBuffer;
	TArray<ChaosInterface::FOverlapHit> ChaosOverlapTouches;

private:
	FSQCapture(FPhysTestSerializer& OwningPhysSerializer);	//This should be created by PhysTestSerializer
	void Serialize(Chaos::FChaosArchive& Ar);

	friend FPhysTestSerializer;

	TArray<uint8> GeomData;
	TArray<uint8> HitData;

	FPhysTestSerializer& PhysSerializer;

	void CaptureChaosFilterResults(const Chaos::FPBDRigidsEvolution& Evolution, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback);

	TMap<Chaos::FGeometryParticle*, TArray<TPair<Chaos::FPerShapeData*, ECollisionQueryHitType>>> ChaosActorToShapeHitsArray;

	template <typename THit>
	void SerializeChaosBuffers(Chaos::FChaosArchive& Ar, int32 Version, ChaosInterface::FSQHitBuffer<THit>& ChaosBuffer);

	void SerializeChaosActorToShapeHitsArray(Chaos::FChaosArchive& Ar);

	bool bDiskDataIsChaos;
	bool bChaosDataReady;
	bool bPhysXDataReady;
};
