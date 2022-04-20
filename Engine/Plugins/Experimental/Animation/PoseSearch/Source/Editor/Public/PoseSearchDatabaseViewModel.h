// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "MovieSceneFwd.h"

#include "PoseSearchDatabasePreviewScene.h"

class UWorld;
class UPoseSearchDatabase;
struct FPoseSearchIndexAsset;
class UAnimPreviewInstance;
class UDebugSkelMeshComponent;

enum class EPoseSearchFeaturesDrawMode : uint8
{
	None,
	All
};

enum class EAnimationPreviewMode : uint8
{
	OriginalOnly,
	OriginalAndMirrored
};


struct FPoseSearchDatabasePreviewActor
{
public:
	TWeakObjectPtr<AActor> Actor = nullptr;
	TWeakObjectPtr<UDebugSkelMeshComponent> Mesh = nullptr;
	TWeakObjectPtr<UAnimPreviewInstance> AnimInstance = nullptr;
	const FPoseSearchIndexAsset* IndexAsset = nullptr;
	int32 CurrentPoseIndex = INDEX_NONE;

	bool IsValid()
	{
		const bool bIsValid = Actor.IsValid() && Mesh.IsValid() && AnimInstance.IsValid();
		return  bIsValid;
	}
};

class FPoseSearchDatabaseViewModel : public TSharedFromThis<FPoseSearchDatabaseViewModel>, public FGCObject
{
public:

	FPoseSearchDatabaseViewModel();
	virtual ~FPoseSearchDatabaseViewModel();

	// ~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FPoseSearchDatabaseViewModel"); }

	void Initialize(
		UPoseSearchDatabase* InPoseSearchDatabase, 
		const TSharedRef<FPoseSearchDatabasePreviewScene>& InPreviewScene);

	void RemovePreviewActors();
	void ResetPreviewActors();
	void RespawnPreviewActors();
	void BuildSearchIndex();

	UPoseSearchDatabase* GetPoseSearchDatabase() const { return PoseSearchDatabase; }
	void OnPreviewActorClassChanged();

	void Tick(float DeltaSeconds);

	TArray<FPoseSearchDatabasePreviewActor>& GetPreviewActors() { return PreviewActors; }
	const TArray<FPoseSearchDatabasePreviewActor>& GetPreviewActors() const { return PreviewActors; }

	void OnSetPoseFeaturesDrawMode(EPoseSearchFeaturesDrawMode DrawMode);
	bool IsPoseFeaturesDrawMode(EPoseSearchFeaturesDrawMode DrawMode) const;

	void OnSetAnimationPreviewMode(EAnimationPreviewMode PreviewMode);
	bool IsAnimationPreviewMode(EAnimationPreviewMode PreviewMode) const;

private:
	float PlayTime = 0.0f;

	/** Scene asset being viewed and edited by this view model. */
	TObjectPtr<UPoseSearchDatabase> PoseSearchDatabase;

	/** Weak pointer to the PreviewScene */
	TWeakPtr<FPoseSearchDatabasePreviewScene> PreviewScenePtr;

	/** Actors to be displayed in the preview viewport */
	TArray<FPoseSearchDatabasePreviewActor> PreviewActors;

	/** What features to show in the viewport */
	EPoseSearchFeaturesDrawMode PoseFeaturesDrawMode = EPoseSearchFeaturesDrawMode::None;

	/** What animations to show in the viewport */
	EAnimationPreviewMode AnimationPreviewMode = EAnimationPreviewMode::OriginalOnly;

	UWorld* GetWorld() const;

	UObject* GetPlaybackContext() const;

	FPoseSearchDatabasePreviewActor SpawnPreviewActor(const FPoseSearchIndexAsset& IndexAsset);

	void UpdatePreviewActors();

	FTransform MirrorRootMotion(FTransform RootMotion, const class UMirrorDataTable* MirrorDataTable);
};