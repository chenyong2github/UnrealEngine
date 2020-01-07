// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigGizmoActor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"

AControlRigGizmoActor::AControlRigGizmoActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bEnabled(true)
	, bSelected(false)
	, bHovered(false)
	, bManipulating(false)
{

	ActorRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent0"));
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent0"));
	StaticMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	StaticMeshComponent->Mobility = EComponentMobility::Movable;
	StaticMeshComponent->SetGenerateOverlapEvents(false);
	StaticMeshComponent->bUseDefaultCollision = true;

	RootComponent = ActorRootComponent;
	StaticMeshComponent->SetupAttachment(RootComponent);
	StaticMeshComponent->bCastStaticShadow = false;
	StaticMeshComponent->bCastDynamicShadow = false;
}

void AControlRigGizmoActor::SetEnabled(bool bInEnabled)
{
	bool bOldEnabled = bEnabled;

	bEnabled = bInEnabled;

	if(bEnabled != bOldEnabled)
	{
		FEditorScriptExecutionGuard Guard;
		OnEnabledChanged(bEnabled);
	}
}

bool AControlRigGizmoActor::IsEnabled() const
{
	return bEnabled;
}

void AControlRigGizmoActor::SetSelected(bool bInSelected)
{
	bool bOldSelected = bSelected;

	bSelected = bInSelected;

	if(bSelected != bOldSelected)
	{
		FEditorScriptExecutionGuard Guard;
		OnSelectionChanged(bSelected);
	}
}

bool AControlRigGizmoActor::IsSelectedInEditor() const
{
	return bSelected;
}

void AControlRigGizmoActor::SetHovered(bool bInHovered)
{
	bool bOldHovered = bHovered;

	bHovered = bInHovered;

	if(bHovered != bOldHovered)
	{
		FEditorScriptExecutionGuard Guard;
		OnHoveredChanged(bHovered);
	}
}

bool AControlRigGizmoActor::IsHovered() const
{
	return bHovered;
}

void AControlRigGizmoActor::SetManipulating(bool bInManipulating)
{
	bool bOldManipulating = bManipulating;

	bManipulating = bInManipulating;

	if(bManipulating != bOldManipulating)
	{
		FEditorScriptExecutionGuard Guard;
		OnManipulatingChanged(bManipulating);
	}
}

bool AControlRigGizmoActor::IsManipulating() const
{
	return bManipulating;
}

// FControlRigGizmoHelper START

namespace FControlRigGizmoHelper
{
	FActorSpawnParameters GetDefaultSpawnParameter()
	{
		FActorSpawnParameters ActorSpawnParameters;
		ActorSpawnParameters.bTemporaryEditorActor = true;
		ActorSpawnParameters.bHideFromSceneOutliner = true;
		ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ActorSpawnParameters.ObjectFlags = RF_Transient;
		return ActorSpawnParameters;
	}

	// create gizmo from custom staticmesh, may deprecate this unless we come up with better usage
	AControlRigGizmoActor* CreateGizmoActor(UWorld* InWorld, UStaticMesh* InStaticMesh, const FGizmoActorCreationParam& CreationParam)
	{
		if (InWorld)
		{
			AControlRigGizmoActor* GizmoActor = CreateDefaultGizmoActor(InWorld, CreationParam);

			if (GizmoActor)
			{
				if (InStaticMesh)
				{
					GizmoActor->StaticMeshComponent->SetStaticMesh(InStaticMesh);
				}

				return GizmoActor;
			}
		}

		return nullptr;
	}

	AControlRigGizmoActor* CreateGizmoActor(UWorld* InWorld, TSubclassOf<AControlRigGizmoActor> InClass, const FGizmoActorCreationParam& CreationParam)
	{
		AControlRigGizmoActor* GizmoActor = InWorld->SpawnActor<AControlRigGizmoActor>(InClass, GetDefaultSpawnParameter());
		if (GizmoActor)
		{
			// set transform
			GizmoActor->SetActorTransform(CreationParam.SpawnTransform);
			return GizmoActor;
		}

		return nullptr;
	}

	AControlRigGizmoActor* CreateDefaultGizmoActor(UWorld* InWorld, const FGizmoActorCreationParam& CreationParam)
	{
		AControlRigGizmoActor* GizmoActor = InWorld->SpawnActor<AControlRigGizmoActor>(AControlRigGizmoActor::StaticClass(), GetDefaultSpawnParameter());
		if (GizmoActor)
		{
			GizmoActor->SetActorTransform(CreationParam.SpawnTransform);

			UStaticMeshComponent* MeshComponent = GizmoActor->StaticMeshComponent;

			if (!CreationParam.StaticMesh.IsValid())
			{
				CreationParam.StaticMesh.LoadSynchronous();
			}
			if (CreationParam.StaticMesh.IsValid())
			{
				MeshComponent->SetStaticMesh(CreationParam.StaticMesh.Get());
				MeshComponent->SetRelativeTransform(CreationParam.MeshTransform * CreationParam.GizmoTransform);
			}

			if (!CreationParam.Material.IsValid())
			{
				CreationParam.Material.LoadSynchronous();
			}
			if (CreationParam.StaticMesh.IsValid())
			{
				UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(CreationParam.Material.Get(), GizmoActor);
				MaterialInstance->SetVectorParameterValue(CreationParam.ColorParameterName, FVector(CreationParam.Color));
				MeshComponent->SetMaterial(0, MaterialInstance);
			}
			return GizmoActor;
		}

		return nullptr;
	}
}

void AControlRigGizmoActor::SetGlobalTransform(const FTransform& InTransform)
{
	if (RootComponent)
	{
		RootComponent->SetRelativeTransform(InTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

FTransform AControlRigGizmoActor::GetGlobalTransform() const
{
	if (RootComponent)
	{
		return RootComponent->GetRelativeTransform();
	}

	return FTransform::Identity;
}
// FControlRigGizmoHelper END