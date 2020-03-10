// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "MovieSceneTrackEditor.h"
#include "TrackEditors/SubTrackEditorBase.h"
#include "ContentBrowserDelegates.h"

struct FAssetData;
class FMenuBuilder;
class UCameraAnim;
class UCameraComponent;
class UTemplateSequence;
class UTemplateSequenceSection;

class FTemplateSequenceTrackEditor : public FMovieSceneTrackEditor
{
public:
	FTemplateSequenceTrackEditor(TSharedRef<ISequencer> InSequencer);

	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

public:
	// ISequencerTrackEditor interface
	virtual bool SupportsType(TSubclassOf<class UMovieSceneTrack> TrackClass) const override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual void AddKey(const FGuid& ObjectGuid) override;

private:
	void AddTemplateSequenceAssetSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, const UClass* RootBindingClass, bool bIsCameraAnimMenu);
	TSharedRef<SWidget> BuildTemplateSequenceAssetSubMenu(FGuid ObjectBinding, const UClass* RootBindingClass, bool bIsCameraAnimMenu);

	void OnTemplateSequenceAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings);
	void OnTemplateSequenceAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings);

	FKeyPropertyResult AddKeyInternal(FFrameNumber KeyTime, FGuid ObjectBinding, UTemplateSequence* TemplateSequence);
	FKeyPropertyResult AddKeyInternal(FFrameNumber KeyTime, TArray<FGuid> ObjectBindings, UTemplateSequence* TemplateSequence);
	FKeyPropertyResult AddLegacyCameraAnimKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, UCameraAnim* CameraAnim);

	bool CanAddSubSequence(const UMovieSceneSequence& Sequence) const;
	const UClass* AcquireObjectClassFromObjectGuid(const FGuid& Guid);
	UCameraComponent* AcquireCameraComponentFromObjectGuid(const FGuid& Guid);

	friend class STemplateSequenceAssetSubMenu;
};

class FTemplateSequenceSection
	: public TSubSectionMixin<>
	, public TSharedFromThis<FTemplateSequenceSection>
{
public:
	FTemplateSequenceSection(TSharedPtr<ISequencer> InSequencer, UTemplateSequenceSection& InSection);
	virtual ~FTemplateSequenceSection() {}
};
