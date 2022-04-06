// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "MovieSceneFwd.h"

class UWorld;
class FPoseSearchDatabasePreviewScene;
class UPoseSearchDatabase;
struct FPoseSearchDatabaseSequence;
class IDetailsView;
class IStructureDetailsView;

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

	void RestartAnimations();
	void BuildSearchIndex();

	UPoseSearchDatabase* GetPoseSearchDatabase() const { return PoseSearchDatabase; }
	void OnPreviewActorClassChanged();

private:

	/** Scene asset being viewed and edited by this view model. */
	TObjectPtr<UPoseSearchDatabase> PoseSearchDatabase;

	/** Weak pointer to the PreviewScene */
	TWeakPtr<FPoseSearchDatabasePreviewScene> PreviewScenePtr;

	AActor* SpawnPreviewActor(const FPoseSearchDatabaseSequence& DatabaseSequence);

	UWorld* GetWorld() const;

	UObject* GetPlaybackContext() const;
};