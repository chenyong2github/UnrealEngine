// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TakeRecorderSource.h"
#include "Library/DMXEntityReference.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "TakeRecorderDMXLibrarySource.generated.h"

class ULevelSequence;
class UMovieSceneFolder;
class UMovieSceneDMXLibraryTrack;
class UMovieSceneDMXLibraryTrackRecorder;

/**
 * Empty struct to have it customized in DetailsView to display a button on
 * the DMX Take Recorder properties. This is a required hack to customize the
 * properties in the TakeRecorder DetailsView because it has a customization
 * that overrides any class customization. So we need to tackle individual
 * property types instead.
 */
USTRUCT()
struct FAddAllPatchesButton
{
	GENERATED_BODY()
};

/** A recording source for DMX data related to a DMX Library */
UCLASS(Category = "DMX", meta = (TakeRecorderDisplayName = "DMX Library"))
class UTakeRecorderDMXLibrarySource
	: public UTakeRecorderSource
{
public:
	GENERATED_BODY()

	UTakeRecorderDMXLibrarySource(const FObjectInitializer& ObjInit);

	/** DMX Library to record Patches' Fixture Functions from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (DisplayName = "DMX Library"))
	UDMXLibrary* DMXLibrary;

	/**
	 * Dummy property to be replaced with the "Add all patches" button.
	 * @see FAddAllPatchesButton
	 */
	UPROPERTY(EditAnywhere, Transient, Category = "My Category", meta = (DisplayName = ""))
	FAddAllPatchesButton AddAllPatchesDummy;

	/** The Fixture Patches to record from the selected Library */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (DisplayName = "Fixture Patches"))
	TArray<FDMXEntityFixturePatchRef> FixturePatchRefs;

	/** Eliminate repeated keyframe values after recording is done */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source")
	bool bReduceKeys;

public:
	/** Adds all Patches from the active DMX Library as recording sources */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void AddAllPatches();

private:
	/** Called when entities were updated */
	void OnEntitiesUpdated(UDMXLibrary* UpdatedLibrary);

	//~ UTakeRecorderSource
	virtual TArray<UTakeRecorderSource*> PreRecording(ULevelSequence* InSequence, ULevelSequence* InMasterSequence, FManifestSerializer* InManifestSerializer) override;
	virtual void StartRecording(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame, class ULevelSequence* InSequence) override;
	virtual void StopRecording(class ULevelSequence* InSequence) override;
	virtual void TickRecording(const FQualifiedFrameTime& CurrentTime) override;
	virtual TArray<UTakeRecorderSource*> PostRecording(class ULevelSequence* InSequence, ULevelSequence* InMasterSequence) override;
	virtual void AddContentsToFolder(UMovieSceneFolder* InFolder) override;
	virtual FText GetDisplayTextImpl() const override;
	//~

	//~ UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
	//~

	/**
	 * Make sure all EntityRefs don't display their Library property and
	 * that they use this source's DMX Library as their library.
	 */
	void ResetPatchesLibrary();

private:
	/** Whether or not we use timecode time or world time*/
	bool bUseSourceTimecode;

	/** Whether to discard livelink samples with timecode that occurs before the start of recording*/
	bool bDiscardSamplesBeforeStart;

	/** Track recorder used by this source */
	UPROPERTY()
	UMovieSceneDMXLibraryTrackRecorder* TrackRecorder;

	/**
	 * Stores an existing DMX Library track in the Sequence to be recorded or
	 * a new one created for recording. Set during PreRecording.
	 */
	TWeakObjectPtr<UMovieSceneDMXLibraryTrack> CachedDMXLibraryTrack;
};
