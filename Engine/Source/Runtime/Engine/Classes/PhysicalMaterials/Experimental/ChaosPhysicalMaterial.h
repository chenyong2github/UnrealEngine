// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "EngineDefines.h"
#include "PhysxUserData.h"
#include "Vehicles/TireType.h"
#include "PhysicsEngine/PhysicsSettingsEnums.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "ChaosPhysicalMaterial.generated.h"

/**
 * Physical materials are used to define the response of a physical object when interacting dynamically with the world.
 */
UCLASS(BlueprintType, Blueprintable, CollapseCategories, HideCategories = Object)
class ENGINE_API UChaosPhysicalMaterial : public UObject
{
	GENERATED_UCLASS_BODY()

	//
	// Surface properties.
	//
	
	/** Friction value of surface, controls how easily things can slide on this surface (0 is frictionless, higher values increase the amount of friction) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PhysicalMaterial, meta=(ClampMin=0))
	float Friction;
	
	/** Restitution or 'bounciness' of this surface, between 0 (no bounce) and 1 (outgoing velocity is same as incoming). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PhysicalMaterial, meta=(ClampMin=0))
	float Restitution;

	/** How much to scale the damage threshold by on any destructible we are applied to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PhysicalMaterial, meta=(ClampMin=0))
	float SleepingLinearVelocityThreshold;
	
	/** How much to scale the damage threshold by on any destructible we are applied to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PhysicalMaterial, meta=(ClampMin=0))
	float SleepingAngularVelocityThreshold;
};



