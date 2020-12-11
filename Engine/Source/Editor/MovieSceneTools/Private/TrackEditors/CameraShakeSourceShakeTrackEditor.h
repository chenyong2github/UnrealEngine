// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SWidget.h"
#include "ISequencer.h"
#include "MovieSceneTrack.h"
#include "ISequencerSection.h"
#include "ISequencerTrackEditor.h"
#include "MovieSceneTrackEditor.h"
#include "Camera/CameraShakeBase.h"

struct FAssetData;
class FMenuBuilder;
class UCameraShakeSourceComponent;

class FCameraShakeSourceShakeTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

	FCameraShakeSourceShakeTrackEditor(TSharedRef<ISequencer> InSequencer);
	virtual ~FCameraShakeSourceShakeTrackEditor() {}

	// ISequencerTrackEditor interface
	virtual UMovieSceneTrack* AddTrack(UMovieScene* FocusedMovieScene, const FGuid& ObjectHandle, TSubclassOf<class UMovieSceneTrack> TrackClass, FName UniqueTypeName) override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;

private:
	// Shake section members
	FKeyPropertyResult AddCameraShakeSectionKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, bool bSelect);
	FKeyPropertyResult AddCameraShakeSectionKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, TSubclassOf<UCameraShakeBase> CameraShake, bool bSelect);

	void AddCameraShakeSection(TArray<FGuid> ObjectHandles);

	TSharedRef<SWidget> BuildCameraShakeSubMenu(FGuid ObjectBinding);
	void AddCameraShakeSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	void AddOtherCameraShakeBrowserSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	void OnCameraShakeAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings);
	void OnCameraShakeAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings);
	void OnAutoCameraShakeSelected(TArray<FGuid> ObjectBindings);
	bool OnShouldFilterCameraShake(const FAssetData& AssetData);

	// Shake trigger members
	FKeyPropertyResult AddCameraShakeTriggerTrackInternal(FFrameNumber Time, const TArray<TWeakObjectPtr<UObject>> Objects, TSubclassOf<UCameraShakeBase> CameraShake);

	void AddCameraShakeTriggerTrack(const TArray<FGuid> ObjectBindings);
	
	// Utility
	UCameraShakeSourceComponent* AcquireCameraShakeSourceComponentFromGuid(const FGuid& Guid);
	void AddCameraShakeTracksMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	TSharedRef<SWidget> BuildCameraShakeTracksMenu(FGuid ObjectBinding);
};

