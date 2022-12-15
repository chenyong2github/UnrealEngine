// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorPreviewScene.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/SkeletalMeshActor.h"

#define LOCTEXT_NAMESPACE "UChaosClothEditorPreviewScene"

void UChaosClothPreviewSceneDescription::SetPreviewScene(FChaosClothPreviewScene* InPreviewScene)
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



FChaosClothPreviewScene::FChaosClothPreviewScene(FPreviewScene::ConstructionValues ConstructionValues) :
	FAdvancedPreviewScene(ConstructionValues)
{
	PreviewSceneDescription = NewObject<UChaosClothPreviewSceneDescription>();
	PreviewSceneDescription->SetPreviewScene(this);

	if (PreviewSceneDescription->SkeletalMeshAsset)
	{
		InitializeSkeletalMeshActor();
	}
}

FChaosClothPreviewScene::~FChaosClothPreviewScene()
{
	if (SkeletalMeshActor)
	{
		SkeletalMeshActor->UnregisterAllComponents();
	}

	if (ClothComponent)
	{
		ClothComponent->UnregisterComponent();
	}
}

void FChaosClothPreviewScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAdvancedPreviewScene::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(PreviewSceneDescription);
	Collector.AddReferencedObject(ClothComponent);
	Collector.AddReferencedObject(SkeletalMeshActor);
}

void FChaosClothPreviewScene::SceneDescriptionPropertyChanged(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, SkeletalMeshAsset))
	{
		if (PreviewSceneDescription->SkeletalMeshAsset)
		{
			InitializeSkeletalMeshActor();
		}
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothPreviewSceneDescription, SkeletalMeshTransform))
	{
		if (SkeletalMeshActor)
		{
			SkeletalMeshActor->SetActorTransform(PreviewSceneDescription->SkeletalMeshTransform);
		}
	}
}


void FChaosClothPreviewScene::InitializeSkeletalMeshActor()
{
	if (SkeletalMeshActor)
	{
		SkeletalMeshActor->UnregisterAllComponents();
	}

	ensure(PreviewSceneDescription->SkeletalMeshAsset);

	SkeletalMeshActor = GetWorld()->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), FTransform::Identity);
	if (SkeletalMeshActor)
	{
		SkeletalMeshActor->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(PreviewSceneDescription->SkeletalMeshAsset);
	}

	SkeletalMeshActor->RegisterAllComponents();
}


void FChaosClothPreviewScene::CreateClothComponent(UChaosClothAsset* Asset)
{
	ClothComponent = NewObject<UChaosClothComponent>();
	ClothComponent->SetClothAsset(Asset);
	ClothComponent->RegisterComponentWithWorld(PreviewWorld);
}


#undef LOCTEXT_NAMESPACE

