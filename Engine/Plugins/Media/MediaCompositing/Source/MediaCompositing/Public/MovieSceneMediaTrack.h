// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"
#include "MovieSceneNameableTrack.h"
#include "UObject/ObjectMacros.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"

#include "MovieSceneMediaTrack.generated.h"

class UMediaSource;
class UMovieSceneMediaSection;
struct FMovieSceneObjectBindingID;


/**
 * Implements a movie scene track for media playback.
 */
UCLASS()
class MEDIACOMPOSITING_API UMovieSceneMediaTrack
	: public UMovieSceneNameableTrack
	, public IMovieSceneTrackTemplateProducer
{
public:

	GENERATED_BODY()

	/**
	 * Create and initialize a new instance.
	 *
	 * @param ObjectInitializer The object initializer.
	 */
	UMovieSceneMediaTrack(const FObjectInitializer& ObjectInitializer);

public:

	/** Adds a new media source to the track. */
	virtual UMovieSceneSection* AddNewMediaSourceOnRow(UMediaSource& MediaSource, FFrameNumber Time, int32 RowIndex);
	/** Adds a new media source to the track. */
	virtual UMovieSceneSection* AddNewMediaSourceProxyOnRow(const FMovieSceneObjectBindingID& ObjectBinding, int32 MediaSourceProxyIndex, FFrameNumber Time, int32 RowIndex);

	/** Adds a new media source on the next available/non-overlapping row. */
	virtual UMovieSceneSection* AddNewMediaSource(UMediaSource& MediaSource, FFrameNumber Time) { return AddNewMediaSourceOnRow(MediaSource, Time, INDEX_NONE); }
	/** Adds a new media source on the next available/non-overlapping row. */
	virtual UMovieSceneSection* AddNewMediaSourceProxy(const FMovieSceneObjectBindingID& ObjectBinding, int32 MediaSourceProxyIndex, FFrameNumber Time)
	{
		return AddNewMediaSourceProxyOnRow(ObjectBinding, MediaSourceProxyIndex, Time, INDEX_NONE);
	}

public:

	//~ UMovieScenePropertyTrack interface

	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool IsEmpty() const override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool SupportsMultipleRows() const override { return true; }


#if WITH_EDITOR
	
	virtual EMovieSceneSectionMovedResult OnSectionMoved(UMovieSceneSection& Section, const FMovieSceneSectionMovedParams& Params);

#endif // WITH_EDITOR

private:
	/** Base function to add a new section. */
	UMovieSceneMediaSection* AddNewSectionOnRow(FFrameNumber Time, int32 RowIndex);

	/** List of all media sections. */
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> MediaSections;

	/**
	 * Updates all our sections so they have correct texture indices
	 * so the proxy can blend all the sections together correctly.
	 */
	void UpdateSectionTextureIndices();

};
