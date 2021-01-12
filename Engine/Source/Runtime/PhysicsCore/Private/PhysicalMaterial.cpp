// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysicalMaterial.cpp
=============================================================================*/ 

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicalMaterials/PhysicalMaterialPropertyBase.h"
#include "UObject/UObjectIterator.h"

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
	StaticFriction = 0.f;
	Restitution = 0.3f;
	RaiseMassToPower = 0.75f;
	Density = 1.0f;
	SleepLinearVelocityThreshold = 1.f;
	SleepAngularVelocityThreshold = 0.05f;
	SleepCounterThreshold = 4;
	DestructibleDamageThresholdScale = 1.0f;
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
	FChaosEngineInterface::UpdateMaterial(*MaterialHandle, this);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void UPhysicalMaterial::RebuildPhysicalMaterials()
{
	for (FThreadSafeObjectIterator Iter(UPhysicalMaterial::StaticClass()); Iter; ++Iter)
	{
		if (UPhysicalMaterial * PhysicalMaterial = Cast<UPhysicalMaterial>(*Iter))
		{
			if(!PhysicalMaterial->MaterialHandle)
			{
				PhysicalMaterial->MaterialHandle = MakeUnique<FPhysicsMaterialHandle>();
			}
			FChaosEngineInterface::UpdateMaterial(*PhysicalMaterial->MaterialHandle, PhysicalMaterial);
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
		if (PhysicalMaterialProperty_DEPRECATED)
		{
			SurfaceType = PhysicalMaterialProperty_DEPRECATED->ConvertToSurfaceType();
		}
	}
}

void UPhysicalMaterial::FinishDestroy()
{
	if(MaterialHandle)
	{
		FChaosEngineInterface::ReleaseMaterial(*MaterialHandle);
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
		*MaterialHandle = FChaosEngineInterface::CreateMaterial(this);
		check(MaterialHandle->IsValid());

		FChaosEngineInterface::SetUserData(*MaterialHandle, &UserData);
		FChaosEngineInterface::UpdateMaterial(*MaterialHandle, this);
	}

	return *MaterialHandle;
}

//This is a bit of a hack, should probably just have a default material live in PhysicsCore instead of in Engine
static UPhysicalMaterial* GEngineDefaultPhysMaterial = nullptr;

void UPhysicalMaterial::SetEngineDefaultPhysMaterial(UPhysicalMaterial* Material)
{
	GEngineDefaultPhysMaterial = Material;
}

EPhysicalSurface UPhysicalMaterial::DetermineSurfaceType(UPhysicalMaterial const* PhysicalMaterial)
{
	if (PhysicalMaterial == NULL)
	{
		PhysicalMaterial = GEngineDefaultPhysMaterial;
	}
	return PhysicalMaterial->SurfaceType;
}
