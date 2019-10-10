// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "MovieSceneTrackEditor.h"

struct FAssetData;
class FMenuBuilder;
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
	void OnTemplateSequenceAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings);
	void OnTemplateSequenceAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings);

	FKeyPropertyResult AddKeyInternal(FFrameNumber KeyTime, TArray<FGuid> ObjectBindings, UTemplateSequence* TemplateSequence);
};

class FTemplateSequenceSection
	: public ISequencerSection
	, public TSharedFromThis<FTemplateSequenceSection>
{
public:

	/** Constructor. */
	FTemplateSequenceSection(UTemplateSequenceSection& InSection);

	/** Virtual destructor. */
	virtual ~FTemplateSequenceSection() {}

	// ISequencerSection interface
	virtual UMovieSceneSection* GetSectionObject() override;
	virtual bool IsReadOnly() const override;
	virtual FText GetSectionTitle() const override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;

private:

	/** The section we are visualizing */
	TWeakObjectPtr<UTemplateSequenceSection> WeakSection;
};
