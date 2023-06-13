// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdvancedPreviewScene.h"
#include "Math/Transform.h"
#include "ClothEditorPreviewScene.generated.h"

class UChaosClothAsset;
class USkeletalMesh;
class UChaosClothComponent;
class FAssetEditorModeManager;
class UAnimationAsset;
class UAnimSingleNodeInstance;

namespace UE::Chaos::ClothAsset
{
class FChaosClothPreviewScene;
}

///
/// The UChaosClothPreviewSceneDescription is a description of the Preview scene contents, intended to be editable in an FAdvancedPreviewSettingsWidget
/// 
UCLASS()
class CHAOSCLOTHASSETEDITOR_API UChaosClothPreviewSceneDescription : public UObject
{
public:
	GENERATED_BODY()

	void SetPreviewScene(UE::Chaos::ClothAsset::FChaosClothPreviewScene* PreviewScene);

	// Skeletal Mesh source asset
	UPROPERTY(EditAnywhere, Category="SkeletalMesh")
	TObjectPtr<USkeletalMesh> SkeletalMeshAsset;

	UPROPERTY(EditAnywhere, Category = "SkeletalMesh")
	FTransform SkeletalMeshTransform;

	UPROPERTY(EditAnywhere, Category = "SkeletalMesh")
	TObjectPtr<UAnimationAsset> AnimationAsset;

private:

	// Listen for changes to the scene description members and notify the PreviewScene
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	UE::Chaos::ClothAsset::FChaosClothPreviewScene* PreviewScene;
};


namespace UE::Chaos::ClothAsset
{
///
/// FChaosClothPreviewScene is the actual Preview scene, with contents specified by the SceneDescription
/// 
class CHAOSCLOTHASSETEDITOR_API FChaosClothPreviewScene : public FAdvancedPreviewScene
{
public:

	FChaosClothPreviewScene(FPreviewScene::ConstructionValues ConstructionValues);
	virtual ~FChaosClothPreviewScene();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	const UChaosClothPreviewSceneDescription* GetPreviewSceneDescription() const { return PreviewSceneDescription; }
	UChaosClothPreviewSceneDescription* GetPreviewSceneDescription() { return PreviewSceneDescription; }

	void SetClothAsset(UChaosClothAsset* Asset);

	// Update Scene in response to the SceneDescription changing
	void SceneDescriptionPropertyChanged(struct FPropertyChangedEvent& PropertyChangedEvent);

	UAnimSingleNodeInstance* GetPreviewAnimInstance();
	const UAnimSingleNodeInstance* const GetPreviewAnimInstance() const;

	UChaosClothComponent* GetClothComponent();
	const UChaosClothComponent* GetClothComponent() const;
	
	const USkeletalMeshComponent* GetSkeletalMeshComponent() const;

	void SetModeManager(TSharedPtr<FAssetEditorModeManager> InClothPreviewEditorModeManager);
	const TSharedPtr<const FAssetEditorModeManager> GetClothPreviewEditorModeManager() const;

private:

	// Create the PreviewAnimationInstance if the AnimationAsset and SkeletalMesh both exist, and set the animation to run on the SkeletalMeshComponent
	void UpdateSkeletalMeshAnimation();

	// Attach the cloth component to the skeletal mesh component, if it exists
	void UpdateClothComponentAttachment();


	void SkeletalMeshTransformChanged(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	void CreateSkeletalMeshComponent();
	void DeleteSkeletalMeshComponent();

	bool IsComponentSelected(const UPrimitiveComponent* InComponent);

	TObjectPtr<UChaosClothPreviewSceneDescription> PreviewSceneDescription;

	TSharedPtr<FAssetEditorModeManager> ClothPreviewEditorModeManager;

	TObjectPtr<UAnimSingleNodeInstance> PreviewAnimInstance;

	TObjectPtr<AActor> SceneActor;

	TObjectPtr<UChaosClothComponent> ClothComponent;

	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

};
} // namespace UE::Chaos::ClothAsset

