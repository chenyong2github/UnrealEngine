// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigGizmoActor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"

AControlRigShapeActor::AControlRigShapeActor(const FObjectInitializer& ObjectInitializer)
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
	StaticMeshComponent->bUseDefaultCollision = false;
#if WITH_EDITORONLY_DATA
	StaticMeshComponent->HitProxyPriority = HPP_Wireframe;
#endif

	RootComponent = ActorRootComponent;
	StaticMeshComponent->SetupAttachment(RootComponent);
	StaticMeshComponent->bCastStaticShadow = false;
	StaticMeshComponent->bCastDynamicShadow = false;
	StaticMeshComponent->bSelectable = bSelectable && bEnabled;
}

void AControlRigShapeActor::SetEnabled(bool bInEnabled)
{
	if(bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
		StaticMeshComponent->bSelectable = bSelectable && bEnabled;
		FEditorScriptExecutionGuard Guard;
		OnEnabledChanged(bEnabled);
	}
}

bool AControlRigShapeActor::IsEnabled() const
{
	return bEnabled;
}

void AControlRigShapeActor::SetSelected(bool bInSelected)
{
	if(bSelected != bInSelected)
	{
		bSelected = bInSelected;
		FEditorScriptExecutionGuard Guard;
		OnSelectionChanged(bSelected);
	}
}

bool AControlRigShapeActor::IsSelectedInEditor() const
{
	return bSelected;
}

void AControlRigShapeActor::SetSelectable(bool bInSelectable)
{
	if (bSelectable != bInSelectable)
	{
		bSelectable = bInSelectable;
		StaticMeshComponent->bSelectable = bSelectable && bEnabled;
		if (!bSelectable)
		{
			SetSelected(false);
		}
	}
}

void AControlRigShapeActor::SetHovered(bool bInHovered)
{
	bool bOldHovered = bHovered;

	bHovered = bInHovered;

	if(bHovered != bOldHovered)
	{
		FEditorScriptExecutionGuard Guard;
		OnHoveredChanged(bHovered);
	}
}

bool AControlRigShapeActor::IsHovered() const
{
	return bHovered;
}


void AControlRigShapeActor::SetShapeColor(const FLinearColor& InColor)
{
	if (StaticMeshComponent && !ColorParameterName.IsNone())
	{
		if (UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(StaticMeshComponent->GetMaterial(0)))
		{
			MaterialInstance->SetVectorParameterValue(ColorParameterName, FVector(InColor));
		}
	}
}

// FControlRigShapeHelper START

namespace FControlRigShapeHelper
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

	// create shape from custom staticmesh, may deprecate this unless we come up with better usage
	AControlRigShapeActor* CreateShapeActor(UWorld* InWorld, UStaticMesh* InStaticMesh, const FControlShapeActorCreationParam& CreationParam)
	{
		if (InWorld)
		{
			AControlRigShapeActor* ShapeActor = CreateDefaultShapeActor(InWorld, CreationParam);

			if (ShapeActor)
			{
				if (InStaticMesh)
				{
					ShapeActor->StaticMeshComponent->SetStaticMesh(InStaticMesh);
				}

				return ShapeActor;
			}
		}

		return nullptr;
	}

	AControlRigShapeActor* CreateShapeActor(UWorld* InWorld, TSubclassOf<AControlRigShapeActor> InClass, const FControlShapeActorCreationParam& CreationParam)
	{
		AControlRigShapeActor* ShapeActor = InWorld->SpawnActor<AControlRigShapeActor>(InClass, GetDefaultSpawnParameter());
		if (ShapeActor)
		{
			// set transform
			ShapeActor->SetActorTransform(CreationParam.SpawnTransform);
			return ShapeActor;
		}

		return nullptr;
	}

	AControlRigShapeActor* CreateDefaultShapeActor(UWorld* InWorld, const FControlShapeActorCreationParam& CreationParam)
	{
		AControlRigShapeActor* ShapeActor = InWorld->SpawnActor<AControlRigShapeActor>(AControlRigShapeActor::StaticClass(), GetDefaultSpawnParameter());
		if (ShapeActor)
		{
			ShapeActor->ControlRigIndex = CreationParam.ControlRigIndex;
			ShapeActor->ControlName = CreationParam.ControlName;
			ShapeActor->SetSelectable(CreationParam.bSelectable);
			ShapeActor->SetActorTransform(CreationParam.SpawnTransform);

			UStaticMeshComponent* MeshComponent = ShapeActor->StaticMeshComponent;

			if (!CreationParam.StaticMesh.IsValid())
			{
				CreationParam.StaticMesh.LoadSynchronous();
			}
			if (CreationParam.StaticMesh.IsValid())
			{
				MeshComponent->SetStaticMesh(CreationParam.StaticMesh.Get());
				MeshComponent->SetRelativeTransform(CreationParam.MeshTransform * CreationParam.ShapeTransform);
			}

			if (!CreationParam.Material.IsValid())
			{
				CreationParam.Material.LoadSynchronous();
			}
			if (CreationParam.StaticMesh.IsValid())
			{
				ShapeActor->ColorParameterName = CreationParam.ColorParameterName;
				UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(CreationParam.Material.Get(), ShapeActor);
				MaterialInstance->SetVectorParameterValue(CreationParam.ColorParameterName, FVector(CreationParam.Color));
				MeshComponent->SetMaterial(0, MaterialInstance);
			}
			return ShapeActor;
		}

		return nullptr;
	}
}

void AControlRigShapeActor::SetGlobalTransform(const FTransform& InTransform)
{
	if (RootComponent)
	{
		RootComponent->SetRelativeTransform(InTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

FTransform AControlRigShapeActor::GetGlobalTransform() const
{
	if (RootComponent)
	{
		return RootComponent->GetRelativeTransform();
	}

	return FTransform::Identity;
}
// FControlRigShapeHelper END