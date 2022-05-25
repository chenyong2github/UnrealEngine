// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "MovieSceneFwd.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimMovieSceneSequence.h"
#include "ISequencer.h"

class UWorld;
class FContextualAnimPreviewScene;
class UContextualAnimSceneAsset;
class UContextualAnimManager;
class UContextualAnimMovieSceneSequence;
class UContextualAnimSceneInstance;
class UMovieScene;
class UMovieSceneTrack;
class UMovieSceneSection;
class UContextualAnimMovieSceneTrack;
class UContextualAnimMovieSceneNotifyTrack;
class UContextualAnimMovieSceneNotifySection;
class UAnimSequenceBase;
class IDetailsView;
class IStructureDetailsView;
class UContextualAnimNewIKTargetParams;
struct FMovieSceneSectionMovedParams;
struct FContextualAnimNewAnimSetParams;
struct FContextualAnimTrack;

class FContextualAnimViewModel : public TSharedFromThis<FContextualAnimViewModel>, public FGCObject
{
public:

	FContextualAnimViewModel();
	virtual ~FContextualAnimViewModel();

	// ~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FContextualAnimViewModel"); }

	void Initialize(UContextualAnimSceneAsset* InSceneAsset, const TSharedRef<FContextualAnimPreviewScene>& InPreviewScene);

	void RefreshSequencerTracks();

	TSharedPtr<ISequencer> GetSequencer();
	TSharedPtr<FContextualAnimPreviewScene> GetPreviewScene();
	UMovieScene* GetMovieScene() const { return MovieScene; }
	UContextualAnimMovieSceneSequence* GetMovieSceneSequence() const { return MovieSceneSequence; }
	UContextualAnimSceneAsset* GetSceneAsset() const { return SceneAsset; }
	UContextualAnimSceneInstance* GetSceneInstance() const { return SceneInstance.Get(); }
	UContextualAnimMovieSceneTrack* FindMasterTrackByRole(const FName& Role) const;

	void AddNewAnimSet(const FContextualAnimNewAnimSetParams& Params);

	void AddNewIKTarget(const UContextualAnimNewIKTargetParams& Params);

	void AnimationModified(UAnimSequenceBase& Animation);

	void SetActiveAnimSetForSection(int32 SectionIdx, int32 AnimSetIdx);

	void OnPreviewActorClassChanged();

	void ToggleSimulateMode();
	bool IsSimulateModeActive() { return bIsSimulateModeActive; }
	void StartSimulation();

	void UpdatePreviewActorTransform(const FContextualAnimSceneBinding& Binding, float Time);

private:

	/** Scene asset being viewed and edited by this view model. */
	TObjectPtr<UContextualAnimSceneAsset> SceneAsset;

	/** MovieSceneSequence for displaying this scene asset in the sequencer time line. */
	TObjectPtr<UContextualAnimMovieSceneSequence> MovieSceneSequence;

	/** MovieScene for displaying this scene asset in the sequencer time line. */
	TObjectPtr<UMovieScene> MovieScene;

	/** Sequencer instance viewing and editing the scene asset */
	TSharedPtr<ISequencer> Sequencer;

	/** Weak pointer to the PreviewScene */
	TWeakPtr<FContextualAnimPreviewScene> PreviewScenePtr;

	TObjectPtr<UContextualAnimManager> ContextualAnimManager;

	TWeakObjectPtr<UContextualAnimSceneInstance> SceneInstance;

	/** Active anim set for each section. Key = SectionIdx, Value = AnimSetIdx  */
	TMap<int32, int32> ActiveAnimSetMap;

	/** The previous play status for sequencer time line. */
	EMovieScenePlayerStatus::Type PreviousSequencerStatus;

	/** The previous time for the sequencer time line. */
	float PreviousSequencerTime = 0.f;

	/** Flag for preventing OnAnimNotifyChanged from updating tracks when the change to the animation came from us */
	bool bUpdatingAnimationFromSequencer = false;

	bool bIsSimulateModeActive = false;

	FContextualAnimStartSceneParams StartSceneParams;

	AActor* SpawnPreviewActor(const FContextualAnimTrack& AnimTrack);

	UWorld* GetWorld() const;

	UObject* GetPlaybackContext() const;

	void SequencerTimeChanged();

	void SequencerDataChanged(EMovieSceneDataChangeType DataChangeType);

	void OnAnimNotifyChanged(UAnimSequenceBase* Animation);

	void CreateSequencer();
};