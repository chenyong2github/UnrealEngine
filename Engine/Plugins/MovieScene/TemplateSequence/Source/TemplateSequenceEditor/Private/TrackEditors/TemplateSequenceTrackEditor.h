// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "MovieSceneTrackEditor.h"
#include "TrackEditors/SubTrackEditorBase.h"

struct FAssetData;
class FMenuBuilder;
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
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;

private:
	void AddTemplateSequenceSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	void AddCameraAnimationSequenceSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	void AddTemplateSequenceAssetSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, const UClass* TemplateSequenceClass, bool bRecursiveClasses = false);

	void OnTemplateSequenceAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings);
	void OnTemplateSequenceAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings);

	FKeyPropertyResult AddKeyInternal(FFrameNumber KeyTime, TArray<FGuid> ObjectBindings, UTemplateSequence* TemplateSequence);

	UCameraComponent* AcquireCameraComponentFromObjectGuid(const FGuid& Guid);
};

class FTemplateSequenceSection
	: public TSubSectionMixin<>
	, public TSharedFromThis<FTemplateSequenceSection>
{
public:
	/** Constructor. */
	FTemplateSequenceSection(TSharedPtr<ISequencer> InSequencer, UTemplateSequenceSection& InSection);

	/** Virtual destructor. */
	virtual ~FTemplateSequenceSection() {}
};
