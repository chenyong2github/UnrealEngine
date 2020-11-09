// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TrackRecorders/MovieSceneTrackRecorder.h"

#include "Library/DMXEntityReference.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Misc/Guid.h"
#include "MovieScene.h"
#include "Containers/Queue.h"

#include "MovieSceneDMXLibraryTrackRecorder.generated.h"

class FDMXSignal;
class UDMXSubsystem;
class UMovieSceneDMXLibraryTrack;
class UMovieSceneDMXLibrarySection;

class UMovieSceneSection;
class UMovieSceneTrackRecorderSettings;

struct FDMXNormalizedAttributeValueMap;

/**
* Track recorder implementation for DMX libraries
* Reuses logic of Animation/LiveLink Plugin in many areas.
*/
UCLASS(BlueprintType)
class DMXEDITOR_API UMovieSceneDMXLibraryTrackRecorder
	: public UMovieSceneTrackRecorder
{
	GENERATED_BODY()
public:
	UMovieSceneDMXLibraryTrackRecorder()
		: LastProcessedTime(0.f)
	{}

	virtual ~UMovieSceneDMXLibraryTrackRecorder() = default;

	// UMovieSceneTrackRecorder Interface
	virtual void RecordSampleImpl(const FQualifiedFrameTime& CurrentFrameTime) override;
	virtual void FinalizeTrackImpl() override;
	virtual void SetSectionStartTimecodeImpl(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame) override;
	virtual UMovieSceneSection* GetMovieSceneSection() const override;
	virtual void StopRecordingImpl() override;
	virtual void SetSavedRecordingDirectory(const FString& InDirectory) { Directory = InDirectory; }
	virtual bool LoadRecordedFile(const FString& InFileName, UMovieScene *InMovieScene, TMap<FGuid, AActor*>& ActorGuidToActorMap, TFunction<void()> InCompletionCallback) override;
	// ~UMovieSceneTrackRecorder Interface

public:
	// Creates a track. We don't call UMovieSceneTrackRecorder::CreateTrack or CreateTrackImpl since that expects an  ObjectToRecord and a GUID which isn't needed.
	void CreateTrack(UMovieScene* InMovieScene, UDMXLibrary* Library, const TArray<FDMXEntityFixturePatchRef>& InFixturePatchRefs, bool bInAlwaysUseTimecode, bool bDiscardSamplesBeforeStart, UMovieSceneTrackRecorderSettings* InSettingsObject);
	void AddContentsToFolder(UMovieSceneFolder* InFolder);
	void SetReduceKeys(bool bInReduce) { bReduceKeys = bInReduce; }

	// Refreshes the recorder. Useful when the underlying DMX Library changed.
	void RefreshTracks();

private:
	/** Called from the network thread when a controller recevies DMX */
	void OnReceiveDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute);

	/** Name of Subject To Record */
	FName SubjectName;

	/** Whether we should save subject preset in the the live link section. If not, we'll create one with subject information with no settings */
	bool bSaveSubjectSettings;

	/** Whether or not we use timecode time or world time*/
	bool bUseSourceTimecode;

	UPROPERTY()
	TArray<FDMXEntityFixturePatchRef> FixturePatchRefs;

	/** Cached DMXLibrary Tracks, section per each maps to SubjectNames */
	TWeakObjectPtr<UMovieSceneDMXLibraryTrack> DMXLibraryTrack;

	/** Sections to record to on each track*/
	TWeakObjectPtr<UMovieSceneDMXLibrarySection> DMXLibrarySection;

	/** The frame at the start of this recording section */
	FFrameNumber RecordStartFrame;

	/** Cached directory for serializers to save to*/
	FString Directory;

	/** Cached Key Reduction from DMX Source Properties*/
	bool bReduceKeys;

	/** Incoming dmx data per patch */
	TMap<UDMXEntityFixturePatch*, TSharedPtr<FDMXSignal>> Buffer;

	/** Time when the next buffer will be read */
	float LastProcessedTime;

private:
	/** Critical section used to get the frame time */
	FCriticalSection FrameTimeCritSec;
};
