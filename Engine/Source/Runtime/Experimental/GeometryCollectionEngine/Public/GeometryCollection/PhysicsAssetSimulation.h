// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosNotifyHandlerInterface.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsInterfaceDeclaresCore.h"

#if INCLUDE_CHAOS

class AActor;
class FSkeletalMeshPhysicsObject;
struct FSkeletalMeshPhysicsObjectParams;
class UChaosPhysicalMaterial;
class UPhysicsAsset;
class USkeletalMeshComponent;


struct GEOMETRYCOLLECTIONENGINE_API FPhysicsAssetSimulationUtil
{
	static void BuildParams(const UObject* Caller, const AActor* OwningActor, const USkeletalMeshComponent* SkelMeshComponent, const UPhysicsAsset* PhysicsAsset, FSkeletalMeshPhysicsObjectParams& OutParams);
	static bool UpdateAnimState(const UObject* Caller, const AActor* OwningActor, const USkeletalMeshComponent* SkelMeshComponent, const float Dt, FSkeletalMeshPhysicsObjectParams& OutParams);
};

#endif
