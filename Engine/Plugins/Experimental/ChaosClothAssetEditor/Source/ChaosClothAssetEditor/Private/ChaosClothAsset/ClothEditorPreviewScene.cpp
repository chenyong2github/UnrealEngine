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
#include "Misc/TransactionObjectEvent.h"
#include "Transforms/TransformGizmoDataBinder.h"


#define LOCTEXT_NAMESPACE "UChaosClothEditorPreviewScene"

void UChaosClothPreviewSceneDescription::SetPreviewScene(UE::Chaos::ClothAsset::FChaosClothPreviewScene* InPreviewScene)
{
	PreviewScene = InPreviewScene;
}

void UChaosClothPreviewSceneDescription::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PreviewScene)
	{
		PreviewScene->SceneDescriptionPropertyChanged(PropertyChangedEvent.GetMemberPropertyName());
	}
}

void UChaosClothPreviewSceneDescription::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);

	// On Undo/Redo, PostEditChangeProperty just gets an empty FPropertyChangedEvent. However this function gets enough info to figure out which property changed
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo && TransactionEvent.HasPropertyChanges())
	{
		const TArray<FName>& PropertyNames = TransactionEvent.GetChangedProperties();
		for (const FName& PropertyName : PropertyNames)
		{
			PreviewScene->SceneDescriptionPropertyChanged(PropertyName);
		}
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

	SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(SceneActor);
	SkeletalMeshComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FChaosClothPreviewScene::IsComponentSelected);
	SkeletalMeshComponent->SetDisablePostProcessBlueprint(true);

	ClothComponent = NewObject<UChaosClothComponent>(SceneActor);
	ClothComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FChaosClothPreviewScene::IsComponentSelected);

	SceneActor->RegisterAllComponents();
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
	check(SkeletalMeshComponent);
	
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
	}
	else
	{
		SkeletalMeshComponent->Stop();
		SkeletalMeshComponent->AnimationData = FSingleAnimationPlayData();
		SkeletalMeshComponent->AnimScriptInstance = nullptr;
	}
}

void FChaosClothPreviewScene::UpdateClothComponentAttachment()
{
	check(SkeletalMeshComponent);
	check(ClothComponent);

	if (SkeletalMeshComponent->GetSkeletalMeshAsset() && !ClothComponent->IsAttachedTo(SkeletalMeshComponent))
	{
		ClothComponent->AttachToComponent(SkeletalMeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	else if (!SkeletalMeshComponent->GetSkeletalMeshAsset() && ClothComponent->IsAttachedTo(SkeletalMeshComponent))
	{
		ClothComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

		// Hard reset cloth simulation if we are losing the attachment
		{
			const FComponentReregisterContext Context(ClothComponent);
		}
	}
}

void FChaosClothPreviewScene::SceneDescriptionPropertyChanged(const FName& PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, SkeletalMeshAsset))
	{
		check(SkeletalMeshComponent);

		SkeletalMeshComponent->SetSkeletalMeshAsset(PreviewSceneDescription->SkeletalMeshAsset);

		UpdateSkeletalMeshAnimation();
		UpdateClothComponentAttachment();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, Translation) || 
		PropertyName == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, Rotation) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, Scale))
	{
		if (DataBinder)
		{
			DataBinder->UpdateAfterDataEdit();
		}
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, AnimationAsset))
	{
		if (!PreviewSceneDescription->AnimationAsset)
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
	check(Asset);
	check(SceneActor);
	check(ClothComponent);

	ClothComponent->SetClothAsset(Asset);
	UpdateClothComponentAttachment();

	// Wait for asset to load and update the component bounds
	ClothComponent->InvalidateCachedBounds();
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

void FChaosClothPreviewScene::SetGizmoDataBinder(TSharedPtr<FTransformGizmoDataBinder> InDataBinder)
{
	DataBinder = InDataBinder;
}

} // namespace UE::Chaos::ClothAsset

#undef LOCTEXT_NAMESPACE

