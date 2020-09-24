// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigGizmoActor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"

AControlRigGizmoActor::AControlRigGizmoActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ControlRigIndex(INDEX_NONE)
	, ControlName(NAME_None)
	, bEnabled(true)
	, bSelected(false)
	, bSelectable(true)
	, bHovered(false)
{

	ActorRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent0"));
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent0"));
	StaticMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	StaticMeshComponent->Mobility = EComponentMobility::Movable;
	StaticMeshComponent->SetGenerateOverlapEvents(false);
	StaticMeshComponent->bUseDefaultCollision = true;
#if WITH_EDITORONLY_DATA
	StaticMeshComponent->HitProxyPriority = HPP_Wireframe;
#endif

	RootComponent = ActorRootComponent;
	StaticMeshComponent->SetupAttachment(RootComponent);
	StaticMeshComponent->bCastStaticShadow = false;
	StaticMeshComponent->bCastDynamicShadow = false;
}

void AControlRigGizmoActor::SetEnabled(bool bInEnabled)
{
	if(bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
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
	if(bSelected != bInSelected)
	{
		bSelected = bInSelected;
		FEditorScriptExecutionGuard Guard;
		OnSelectionChanged(bSelected);
	}
}

bool AControlRigGizmoActor::IsSelectedInEditor() const
{
	return bSelected;
}

void AControlRigGizmoActor::SetSelectable(bool bInSelectable)
{
	if (bSelectable != bInSelectable)
	{
		bSelectable = bInSelectable;
		if (!bSelectable)
		{
			SetSelected(false);
		}
	}
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


void AControlRigGizmoActor::SetGizmoColor(const FLinearColor& InColor)
{
	if (StaticMeshComponent && !ColorParameterName.IsNone())
	{
		if (UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(StaticMeshComponent->GetMaterial(0)))
		{
			MaterialInstance->SetVectorParameterValue(ColorParameterName, FVector(InColor));
		}
	}
}

// FControlRigGizmoHelper START

namespace FControlRigGizmoHelper
{
	FActorSpawnParameters GetDefaultSpawnParameter()
	{
		FActorSpawnParameters ActorSpawnParameters;
#if WITH_EDITOR
		ActorSpawnParameters.bTemporaryEditorActor = true;
		ActorSpawnParameters.bHideFromSceneOutliner = true;
#endif
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
			GizmoActor->ControlRigIndex = CreationParam.ControlRigIndex;
			GizmoActor->ControlName = CreationParam.ControlName;
			GizmoActor->SetSelectable(CreationParam.bSelectable);
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
				GizmoActor->ColorParameterName = CreationParam.ColorParameterName;
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