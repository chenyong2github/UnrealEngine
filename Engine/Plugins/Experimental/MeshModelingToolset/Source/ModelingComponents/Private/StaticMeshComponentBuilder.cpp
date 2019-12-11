// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshComponentBuilder.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"



FStaticMeshComponentBuilder::FStaticMeshComponentBuilder()
{
	NewStaticMesh = nullptr;
}

void FStaticMeshComponentBuilder::Initialize(UPackage* AssetPackage, FName MeshName, int NumMaterialSlots)
{
	// create new UStaticMesh object
	EObjectFlags flags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
	this->NewStaticMesh = NewObject<UStaticMesh>(AssetPackage, MeshName, flags);

#if WITH_EDITOR
	// initialize the LOD 0 MeshDescription
	NewStaticMesh->SetNumSourceModels(1);
	NewStaticMesh->GetSourceModel(0).BuildSettings.bRecomputeNormals = false;
	NewStaticMesh->GetSourceModel(0).BuildSettings.bRecomputeTangents = true;
	this->MeshDescription = NewStaticMesh->CreateMeshDescription(0);
#endif

	if (NewStaticMesh->BodySetup == nullptr)
	{
		NewStaticMesh->CreateBodySetup();
	}
	if (NewStaticMesh->BodySetup != nullptr)
	{
		// enable complex as simple collision to use mesh directly
		NewStaticMesh->BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	}

	// add a material slot. Must always have one material slot.
	int AddMaterialCount = FMath::Max(1, NumMaterialSlots);
	for (int MatIdx = 0; MatIdx < AddMaterialCount; MatIdx++)
	{
		NewStaticMesh->StaticMaterials.Add(FStaticMaterial());
	}
}



void FStaticMeshComponentBuilder::CreateAndSetAsRootComponent(AActor* Actor)
{
#if WITH_EDITOR
	// assuming we have updated the LOD 0 MeshDescription, tell UStaticMesh about this
	NewStaticMesh->CommitMeshDescription(0);
#endif
	// if we have a StaticMeshActor we already have a StaticMeshComponent, otherwise we 
	// need to make a new one. Note that if we make a new one it will not be editable in the 
	// Editor because it is not a UPROPERTY...
	AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
	if (StaticMeshActor != nullptr)
	{
		NewMeshComponent = StaticMeshActor->GetStaticMeshComponent();
	}
	else 
	{
		// create component
		NewMeshComponent = NewObject<UStaticMeshComponent>(Actor);
		Actor->SetRootComponent(NewMeshComponent);
	}

	// this disconnects the component from various events
	NewMeshComponent->UnregisterComponent();

	// Configure flags of the component. Is this necessary?
	NewMeshComponent->SetMobility(EComponentMobility::Movable);
	NewMeshComponent->bSelectable = true;

	// replace the UStaticMesh in the component
	NewMeshComponent->SetStaticMesh(NewStaticMesh);

	// UActorFactoryBasicShape::PostSpawnActor does this...what for??
	//NewMeshComponent->StaticMeshDerivedDataKey = StaticMesh->RenderData->DerivedDataKey;

	// call this to set a different default material. WorldMaterial shows UV so is more useful...
	//UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	//check(Material != nullptr);
	//NewMeshComponent->SetMaterial(0, Material);

	// re-connect the component (?)
	NewMeshComponent->RegisterComponent();

#if WITH_EDITOR
	// not sure what this does...marks things dirty? updates stuff after modification??
	NewStaticMesh->PostEditChange();
#endif

	// do we need to do this?
	//NewMeshComponent->PostEditChange();

	// do we need to do any of these? or is default collision profile sufficient?
	//NewStaticMesh->BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	//NewMeshComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	//NewMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	// if we don't do this, world traces don't hit the mesh
	NewMeshComponent->RecreatePhysicsState();
}
