// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigThumbnailRenderer.h"
#include "ThumbnailHelpers.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ControlRig.h"

UControlRigThumbnailRenderer::UControlRigThumbnailRenderer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RigBlueprint = nullptr;
}

bool UControlRigThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	if (UControlRigBlueprint* InRigBlueprint = Cast<UControlRigBlueprint>(Object))
	{
		USkeletalMesh* SkeletalMesh = InRigBlueprint->PreviewSkeletalMesh.Get();
		if (SkeletalMesh != nullptr)
		{
			if (InRigBlueprint->GizmoLibrary.IsValid())
			{
				for (const FRigControl& Control : InRigBlueprint->HierarchyContainer.ControlHierarchy)
				{
					if (const FControlRigGizmoDefinition* GizmoDef = InRigBlueprint->GizmoLibrary->GetGizmoByName(Control.GizmoName))
					{
						UStaticMesh* StaticMesh = GizmoDef->StaticMesh.Get();
						if (StaticMesh == nullptr) // not yet loaded
						{
							return false;
						}
					}
					return true;
				}
			}
		}

	}
	return false;
}

void UControlRigThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	RigBlueprint = nullptr;

	if (UControlRigBlueprint* InRigBlueprint = Cast<UControlRigBlueprint>(Object))
	{
		USkeletalMesh* SkeletalMesh = InRigBlueprint->PreviewSkeletalMesh.Get();
		if (SkeletalMesh != nullptr)
		{
			RigBlueprint = InRigBlueprint;
			Super::Draw(SkeletalMesh, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);

			for (auto Pair : GizmoActors)
			{
				if (Pair.Value && Pair.Value->GetOuter())
				{
					Pair.Value->Rename(nullptr, GetTransientPackage());
					Pair.Value->MarkPendingKill();
				}
			}
			GizmoActors.Reset();
		}
	}
}

void UControlRigThumbnailRenderer::AddAdditionalPreviewSceneContent(UObject* Object, UWorld* PreviewWorld)
{
	if (ThumbnailScene && ThumbnailScene->GetPreviewActor() && RigBlueprint && RigBlueprint->GizmoLibrary && RigBlueprint->GeneratedClass)
	{
		UControlRig* ControlRig = nullptr;

		// reuse the current control rig if possible
		UControlRig* CDO = Cast<UControlRig>(RigBlueprint->GeneratedClass->GetDefaultObject(true /* create if needed */));

		TArray<UObject*> ArchetypeInstances;
		CDO->GetArchetypeInstances(ArchetypeInstances);
		for (UObject* ArchetypeInstance : ArchetypeInstances)
		{
			ControlRig = Cast<UControlRig>(ArchetypeInstance);
			break;
		}

		if (ControlRig == nullptr)
		{
			// fall back to the CDO. we only need to pull out
			// the pose of the default hierarchy so the CDO is fine.
			// this case only happens if the editor had been closed
			// and there are no archetype instances left.
			ControlRig = CDO;
		}

		FTransform ComponentToWorld = ThumbnailScene->GetPreviewActor()->GetSkeletalMeshComponent()->GetComponentToWorld();

		for (const FRigControl& Control : ControlRig->GetControlHierarchy())
		{
			switch (Control.ControlType)
			{
				case ERigControlType::Float:
				case ERigControlType::Integer:
				case ERigControlType::Vector2D:
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				case ERigControlType::Transform:
				case ERigControlType::TransformNoScale:
				case ERigControlType::EulerTransform:
				{
					if (const FControlRigGizmoDefinition* GizmoDef = RigBlueprint->GizmoLibrary->GetGizmoByName(Control.GizmoName))
					{
						FTransform GizmoTransform = Control.GizmoTransform * (GizmoDef->Transform * ControlRig->GetControlGlobalTransform(Control.Name)) * ComponentToWorld;
						UStaticMesh* StaticMesh = GizmoDef->StaticMesh.Get();
						if (StaticMesh == nullptr) // not yet loaded
						{
							continue;
						}

						FActorSpawnParameters SpawnInfo;
						SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						SpawnInfo.bNoFail = true;
						SpawnInfo.ObjectFlags = RF_Transient;
						AStaticMeshActor* GizmoActor = PreviewWorld->SpawnActor<AStaticMeshActor>(SpawnInfo);
						GizmoActor->SetActorEnableCollision(false);

						UMaterial* DefaultMaterial = RigBlueprint->GizmoLibrary->DefaultMaterial.Get();
						if (DefaultMaterial == nullptr) // not yet loaded
						{
							continue;
						}
						UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(DefaultMaterial, GizmoActor);
						MaterialInstance->SetVectorParameterValue(RigBlueprint->GizmoLibrary->MaterialColorParameter, FVector(Control.GizmoColor));
						GizmoActor->GetStaticMeshComponent()->SetMaterial(0, MaterialInstance);

						GizmoActors.Add(Control.Name, GizmoActor);

						GizmoActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
						GizmoActor->SetActorTransform(GizmoTransform);
					}
					break;
				}
				default:
				{
					break;
				}
			}
		}
	}
}