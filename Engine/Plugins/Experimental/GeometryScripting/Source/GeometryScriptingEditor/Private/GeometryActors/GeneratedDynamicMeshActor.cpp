// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryActors/GeneratedDynamicMeshActor.h"
#include "GeometryActors/EditorGeometryGenerationSubsystem.h"

#include "Editor/EditorEngine.h" // for CopyPropertiesForUnrelatedObjects
#include "Engine/StaticMeshActor.h"

#define LOCTEXT_NAMESPACE "AGeneratedDynamicMeshActor"


AGeneratedDynamicMeshActor::AGeneratedDynamicMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RegisterWithGenerationManager();
}

AGeneratedDynamicMeshActor::~AGeneratedDynamicMeshActor()
{
	UnregisterWithGenerationManager();
}


void AGeneratedDynamicMeshActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	bGeneratedMeshRebuildPending = true;
}


void AGeneratedDynamicMeshActor::Destroyed()
{
	UnregisterWithGenerationManager();
	Super::Destroyed();
}


void AGeneratedDynamicMeshActor::RegisterWithGenerationManager()
{
	if (HasAnyFlags(RF_ClassDefaultObject))		// ignore for CDO
	{
		return;
	}

	if (bIsRegisteredWithGenerationManager == false)
	{
		UEditorGeometryGenerationSubsystem::RegisterGeneratedMeshActor(this);
		bIsRegisteredWithGenerationManager = true;
	}
}

void AGeneratedDynamicMeshActor::UnregisterWithGenerationManager()
{
	if (HasAnyFlags(RF_ClassDefaultObject))		// ignore for CDO
	{
		return;
	}

	if (bIsRegisteredWithGenerationManager)
	{
		UEditorGeometryGenerationSubsystem::UnregisterGeneratedMeshActor(this);
		bIsRegisteredWithGenerationManager = false;
		bGeneratedMeshRebuildPending = false;
	}
}



void AGeneratedDynamicMeshActor::ExecuteRebuildGeneratedMeshIfPending()
{
	if (bGeneratedMeshRebuildPending)
	{
		// Automatically defer collision updates during generated mesh rebuild. If we do not do this, then
		// each mesh change will result in collision being rebuilt, which is very expensive !!
		bool bEnabledDeferredCollision = false;
		if (DynamicMeshComponent->bDeferCollisionUpdates == false)
		{
			DynamicMeshComponent->SetDeferredCollisionUpdatesEnabled(true, false);
			bEnabledDeferredCollision = true;
		}

		if (bResetOnRebuild && DynamicMeshComponent && DynamicMeshComponent->GetDynamicMesh())
		{
			DynamicMeshComponent->GetDynamicMesh()->Reset();
		}

		FEditorScriptExecutionGuard Guard;
		OnRebuildGeneratedMesh(DynamicMeshComponent->GetDynamicMesh());
		bGeneratedMeshRebuildPending = false;

		if (bEnabledDeferredCollision)
		{
			DynamicMeshComponent->SetDeferredCollisionUpdatesEnabled(false, true);
		}
	}
}




void AGeneratedDynamicMeshActor::CopyPropertiesToStaticMesh(AStaticMeshActor* StaticMeshActor, bool bCopyComponentMaterials)
{
	StaticMeshActor->Modify();
	StaticMeshActor->UnregisterAllComponents();
	UEditorEngine::CopyPropertiesForUnrelatedObjects(this, StaticMeshActor);

	if (bCopyComponentMaterials)
	{
		if (UStaticMeshComponent* SMComponent = StaticMeshActor->GetStaticMeshComponent())
		{
			if (UDynamicMeshComponent* DMComponent = this->GetDynamicMeshComponent())
			{
				TArray<UMaterialInterface*> Materials = DMComponent->GetMaterials();
				for (int32 k = 0; k < Materials.Num(); ++k)
				{
					SMComponent->SetMaterial(k, Materials[k]);
				}
			}
		}
	}

	StaticMeshActor->ReregisterAllComponents();
}


void AGeneratedDynamicMeshActor::CopyPropertiesFromStaticMesh(AStaticMeshActor* StaticMeshActor, bool bCopyComponentMaterials)
{
	this->Modify();
	this->UnregisterAllComponents();
	UEditorEngine::CopyPropertiesForUnrelatedObjects(StaticMeshActor, this);

	if (bCopyComponentMaterials)
	{
		if (UStaticMeshComponent* SMComponent = StaticMeshActor->GetStaticMeshComponent())
		{
			if (UDynamicMeshComponent* DMComponent = this->GetDynamicMeshComponent())
			{
				TArray<UMaterialInterface*> Materials = SMComponent->GetMaterials();
				DMComponent->ConfigureMaterialSet(Materials);
			}
		}
	}

	this->ReregisterAllComponents();
}


#undef LOCTEXT_NAMESPACE