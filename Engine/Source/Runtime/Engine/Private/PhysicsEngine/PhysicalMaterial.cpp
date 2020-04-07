// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysicalMaterial.cpp
=============================================================================*/ 

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/UObjectIterator.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "PhysicsPublic.h"
#include "PhysicalMaterials/PhysicalMaterialPropertyBase.h"
#include "PhysicsEngine/PhysicsSettings.h"

#if PHYSICS_INTERFACE_PHYSX
	#include "PhysicsEngine/PhysXSupport.h"
	#include "Physics/PhysicsInterfacePhysX.h"
#endif // WITH_PHYSX

#if WITH_CHAOS
	#include "Chaos/PhysicalMaterials.h"
#endif

UDEPRECATED_PhysicalMaterialPropertyBase::UDEPRECATED_PhysicalMaterialPropertyBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UPhysicalMaterial::UPhysicalMaterial(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Friction = 0.7f;
	Restitution = 0.3f;
	RaiseMassToPower = 0.75f;
	Density = 1.0f;
	SleepLinearVelocityThreshold = 0.001f;
	SleepAngularVelocityThreshold = 0.0087f;
	SleepCounterThreshold = 0;
	DestructibleDamageThresholdScale = 1.0f;
	TireFrictionScale = 1.0f;
	bOverrideFrictionCombineMode = false;
	UserData = FChaosUserData(this);
}

UPhysicalMaterial::UPhysicalMaterial(FVTableHelper& Helper)
	: Super(Helper)
{
}

UPhysicalMaterial::~UPhysicalMaterial() = default;

#if WITH_EDITOR
void UPhysicalMaterial::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if(!MaterialHandle)
	{
		MaterialHandle = MakeUnique<FPhysicsMaterialHandle>();
	}
	// Update PhysX material last so we have a valid Parent
	FPhysicsInterface::UpdateMaterial(*MaterialHandle, this);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void UPhysicalMaterial::RebuildPhysicalMaterials()
{
	for (FObjectIterator Iter(UPhysicalMaterial::StaticClass()); Iter; ++Iter)
	{
		if (UPhysicalMaterial * PhysicalMaterial = Cast<UPhysicalMaterial>(*Iter))
		{
			if(!PhysicalMaterial->MaterialHandle)
			{
				PhysicalMaterial->MaterialHandle = MakeUnique<FPhysicsMaterialHandle>();
			}
			FPhysicsInterface::UpdateMaterial(*PhysicalMaterial->MaterialHandle, PhysicalMaterial);
		}
	}
}

#endif // WITH_EDITOR

void UPhysicalMaterial::PostLoad()
{
	Super::PostLoad();

	// we're removing physical material property, so convert to Material type
	if (GetLinkerUE4Version() < VER_UE4_REMOVE_PHYSICALMATERIALPROPERTY)
	{
		if (PhysicalMaterialProperty)
		{
			SurfaceType = PhysicalMaterialProperty->ConvertToSurfaceType();
		}
	}
}

void UPhysicalMaterial::FinishDestroy()
{
	if(MaterialHandle)
	{
		FPhysicsInterface::ReleaseMaterial(*MaterialHandle);
	}
	Super::FinishDestroy();
}

FPhysicsMaterialHandle& UPhysicalMaterial::GetPhysicsMaterial()
{
	if(!MaterialHandle)
	{
		MaterialHandle = MakeUnique<FPhysicsMaterialHandle>();
	}
	if(!MaterialHandle->IsValid())
	{
		*MaterialHandle = FPhysicsInterface::CreateMaterial(this);
		check(MaterialHandle->IsValid());

		FPhysicsInterface::SetUserData(*MaterialHandle, &UserData);
		FPhysicsInterface::UpdateMaterial(*MaterialHandle, this);
	}

	return *MaterialHandle;
}

EPhysicalSurface UPhysicalMaterial::DetermineSurfaceType(UPhysicalMaterial const* PhysicalMaterial)
{
	if (PhysicalMaterial == NULL)
	{
		PhysicalMaterial = GEngine->DefaultPhysMaterial;
	}
	
	return PhysicalMaterial->SurfaceType;
}
