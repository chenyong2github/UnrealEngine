// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorPreviewScene.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "AssetEditorModeManager.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "ComponentReregisterContext.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "SkinnedAssetCompiler.h"

#define LOCTEXT_NAMESPACE "UChaosClothEditorPreviewScene"

void UChaosClothPreviewSceneDescription::SetPreviewScene(UE::Chaos::ClothAsset::FChaosClothPreviewScene* InPreviewScene)
{
	PreviewScene = InPreviewScene;
}

void UChaosClothPreviewSceneDescription::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PreviewScene)
	{
		PreviewScene->SceneDescriptionPropertyChanged(PropertyChangedEvent);
	}
}

namespace UE::Chaos::ClothAsset
{

FChaosClothPreviewScene::FChaosClothPreviewScene(FPreviewScene::ConstructionValues ConstructionValues) :
	FAdvancedPreviewScene(ConstructionValues)
{
	PreviewSceneDescription = NewObject<UChaosClothPreviewSceneDescription>();
	PreviewSceneDescription->SetPreviewScene(this);

	SceneActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());

	if (PreviewSceneDescription->SkeletalMeshAsset)
	{
		CreateSkeletalMeshComponent();
		UpdateSkeletalMeshAnimation();
	}
}

FChaosClothPreviewScene::~FChaosClothPreviewScene()
{
	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->TransformUpdated.RemoveAll(this);
		SkeletalMeshComponent->SelectionOverrideDelegate.Unbind();
		SkeletalMeshComponent->UnregisterComponent();
	}

	if (ClothComponent)
	{
		ClothComponent->SelectionOverrideDelegate.Unbind();
		ClothComponent->UnregisterComponent();
	}
}

void FChaosClothPreviewScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAdvancedPreviewScene::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(PreviewSceneDescription);
	Collector.AddReferencedObject(ClothComponent);
	Collector.AddReferencedObject(SkeletalMeshComponent);
	Collector.AddReferencedObject(SceneActor);
	Collector.AddReferencedObject(PreviewAnimInstance);
}


void FChaosClothPreviewScene::UpdateSkeletalMeshAnimation()
{
	if (SkeletalMeshComponent)
	{
		if (PreviewSceneDescription->AnimationAsset)
		{
			PreviewAnimInstance = NewObject<UAnimSingleNodeInstance>(SkeletalMeshComponent);
			PreviewAnimInstance->SetAnimationAsset(PreviewSceneDescription->AnimationAsset);

			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			SkeletalMeshComponent->InitAnim(true);
			SkeletalMeshComponent->AnimationData.PopulateFrom(PreviewAnimInstance);
			SkeletalMeshComponent->AnimScriptInstance = PreviewAnimInstance;
			SkeletalMeshComponent->AnimScriptInstance->InitializeAnimation();
			SkeletalMeshComponent->ValidateAnimation();
			SkeletalMeshComponent->Stop();
		}
		else
		{
			SkeletalMeshComponent->Stop();
			SkeletalMeshComponent->AnimationData = FSingleAnimationPlayData();
			SkeletalMeshComponent->AnimScriptInstance = nullptr;
		}
	}
}

void FChaosClothPreviewScene::UpdateClothComponentAttachment()
{
	if (ClothComponent && SkeletalMeshComponent && !ClothComponent->IsAttachedTo(SkeletalMeshComponent))
	{
		ClothComponent->AttachToComponent(SkeletalMeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

void FChaosClothPreviewScene::SceneDescriptionPropertyChanged(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, SkeletalMeshAsset))
	{
		if (PreviewSceneDescription->SkeletalMeshAsset)
		{
			CreateSkeletalMeshComponent();
		}
		else
		{
			DeleteSkeletalMeshComponent();
		}
		UpdateSkeletalMeshAnimation();
		UpdateClothComponentAttachment();
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, SkeletalMeshTransform))
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetComponentToWorld(PreviewSceneDescription->SkeletalMeshTransform);
		}
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, AnimationAsset))
	{
		if (!SkeletalMeshComponent || !PreviewSceneDescription->AnimationAsset)
		{
			PreviewAnimInstance = nullptr;
		}
		UpdateSkeletalMeshAnimation();
	}
	
}

UChaosClothComponent* FChaosClothPreviewScene::GetClothComponent()
{
	return ClothComponent;
}

const UChaosClothComponent* FChaosClothPreviewScene::GetClothComponent() const
{
	return ClothComponent;
}

const USkeletalMeshComponent* FChaosClothPreviewScene::GetSkeletalMeshComponent() const
{
	return SkeletalMeshComponent;
}

void FChaosClothPreviewScene::SetModeManager(TSharedPtr<FAssetEditorModeManager> InClothPreviewEditorModeManager)
{
	ClothPreviewEditorModeManager = InClothPreviewEditorModeManager;
}

const TSharedPtr<const FAssetEditorModeManager> FChaosClothPreviewScene::GetClothPreviewEditorModeManager() const
{
	return ClothPreviewEditorModeManager;
}

void FChaosClothPreviewScene::SkeletalMeshTransformChanged(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	ensure(UpdatedComponent == SkeletalMeshComponent);
	PreviewSceneDescription->SkeletalMeshTransform = UpdatedComponent->GetComponentToWorld();
}


void FChaosClothPreviewScene::DeleteSkeletalMeshComponent()
{
	if (SkeletalMeshComponent)
	{
		if (SceneActor)
		{
			SceneActor->RemoveOwnedComponent(SkeletalMeshComponent);
		}

		SkeletalMeshComponent->TransformUpdated.RemoveAll(this);
		SkeletalMeshComponent->SelectionOverrideDelegate.Unbind();
		SkeletalMeshComponent->UnregisterComponent();
	}

	SkeletalMeshComponent = nullptr;

	// Detach the ClothComponent and make it the RootComponent of the SceneActor
	if (SceneActor && ClothComponent)
	{
		const bool bWasSimming = !ClothComponent->IsSimulationSuspended();
		ClothComponent->SuspendSimulation();

		ClothComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		SceneActor->SetRootComponent(ClothComponent);
		SceneActor->RegisterAllComponents();

		// hard reset and resume simulation if it was running prior
		{
			const FComponentReregisterContext Context(ClothComponent);
		}
		if (bWasSimming)
		{
			ClothComponent->ResumeSimulation();
		}
	}
}

void FChaosClothPreviewScene::CreateSkeletalMeshComponent()
{
	ensure(PreviewSceneDescription->SkeletalMeshAsset);

	// Remove any existing skeletal mesh component
	DeleteSkeletalMeshComponent();

	SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(SceneActor);
	SkeletalMeshComponent->SetSkeletalMeshAsset(PreviewSceneDescription->SkeletalMeshAsset);
	SkeletalMeshComponent->TransformUpdated.AddRaw(this, &FChaosClothPreviewScene::SkeletalMeshTransformChanged);
	SkeletalMeshComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FChaosClothPreviewScene::IsComponentSelected);

	SceneActor->SetRootComponent(SkeletalMeshComponent);
	SceneActor->RegisterAllComponents();
}

bool FChaosClothPreviewScene::IsComponentSelected(const UPrimitiveComponent* InComponent)
{
	if (const UTypedElementSelectionSet* const TypedElementSelectionSet = ClothPreviewEditorModeManager->GetEditorSelectionSet())
	{
		if (const FTypedElementHandle ComponentElement = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent))
		{
			const bool bElementSelected = TypedElementSelectionSet->IsElementSelected(ComponentElement, FTypedElementIsSelectedOptions());
			return bElementSelected;
		}
	}

	return false;
}

void FChaosClothPreviewScene::SetClothAsset(UChaosClothAsset* Asset)
{
	// Clean up old cloth component if it exists

	check(Asset);
	check(SceneActor);

	if (ClothComponent)
	{
		ClothComponent->SetClothAsset(Asset);
	}
	else
	{
		ClothComponent = NewObject<UChaosClothComponent>(SceneActor);
		ClothComponent->SetClothAsset(Asset);
		ClothComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FChaosClothPreviewScene::IsComponentSelected);

		if (SkeletalMeshComponent)
		{
			check(SceneActor->GetRootComponent() == SkeletalMeshComponent);
		}
		else
		{
			SceneActor->SetRootComponent(ClothComponent);
		}

		SceneActor->RegisterAllComponents();
	}

	UpdateClothComponentAttachment();

	FSkinnedAssetCompilingManager::Get().FinishCompilation(TArrayView<USkinnedAsset* const>{Asset});
	ClothComponent->UpdateBounds();
}

UAnimSingleNodeInstance* FChaosClothPreviewScene::GetPreviewAnimInstance()
{
	return PreviewAnimInstance;
}

const UAnimSingleNodeInstance* const FChaosClothPreviewScene::GetPreviewAnimInstance() const
{
	return PreviewAnimInstance;
}
} // namespace UE::Chaos::ClothAsset

#undef LOCTEXT_NAMESPACE

