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
		FEditorScriptExecutionGuard Guard;
		OnRebuildGeneratedMesh();
		bGeneratedMeshRebuildPending = false;
	}
}


#undef LOCTEXT_NAMESPACE