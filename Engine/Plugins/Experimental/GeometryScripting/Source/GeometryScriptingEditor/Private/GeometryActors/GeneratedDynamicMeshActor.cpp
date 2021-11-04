// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryActors/GeneratedDynamicMeshActor.h"
#include "GeometryActors/EditorGeometryGenerationSubsystem.h"

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


#undef LOCTEXT_NAMESPACE