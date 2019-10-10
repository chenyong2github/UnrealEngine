// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Sections/MovieSceneParameterSection.h"
#include "MovieSceneNameableTrack.h"
#include "ControlRig.h"
#include "MovieSceneControlRigParameterTrack.generated.h"

/**
 * Handles animation of skeletal mesh actors using animation ControlRigs
 */

class IControlRigManipulatable;

UCLASS(MinimalAPI)
class UMovieSceneControlRigParameterTrack
	: public UMovieSceneNameableTrack
{
	GENERATED_UCLASS_BODY()

public:

	// UMovieSceneTrack interface
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void RemoveAllAnimationData() override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool IsEmpty() const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual FName GetTrackName() const override { return TrackName; }

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif
public:
	/** Add a section at that start time*/
	CONTROLRIG_API UMovieSceneSection* CreateControlRigSection(FFrameNumber StartTime, UControlRig* InControlRig);

	IControlRigManipulatable* GetManipulatableFromBinding(UMovieScene* MovieScene);

public:
	CONTROLRIG_API UControlRig* GetControlRig() const { return ControlRig; }

	/**
	* Find all sections at the current time.
	*
	*@param Time  The Time relative to the owning movie scene where the section should be
	*@Return All sections at that time
	*/
	CONTROLRIG_API TArray<UMovieSceneSection*, TInlineAllocator<4>> FindAllSections(FFrameNumber Time);

	/**
	 * Finds a section at the current time.
	 *
	 * @param Time The time relative to the owning movie scene where the section should be
	 * @return The found section.
	 */
	CONTROLRIG_API class UMovieSceneSection* FindSection(FFrameNumber Time);

	/**
	 * Finds a section at the current time or extends an existing one
	 *
	 * @param Time The time relative to the owning movie scene where the section should be
	 * @param OutWeight The weight of the section if found
	 * @return The found section.
	 */
	CONTROLRIG_API class UMovieSceneSection* FindOrExtendSection(FFrameNumber Time, float& OutWeight);

	/**
	 * Finds a section at the current time.
	 *
	 * @param Time The time relative to the owning movie scene where the section should be
	 * @param bSectionAdded Whether a section was added or not
	 * @return The found section, or the new section.
	 */
	CONTROLRIG_API class UMovieSceneSection* FindOrAddSection(FFrameNumber Time, bool& bSectionAdded);

	/**
	 * Set the section we want to key and recieve globally changed values.
	 *
	 * @param Section The section that changes.
	 */

	virtual void SetSectionToKey(UMovieSceneSection* Section) override;

	/**
	 * Finds a section we want to key and recieve globally changed values.
	 * @return The Section that changes.
	 */
	virtual UMovieSceneSection* GetSectionToKey() const override;

	CONTROLRIG_API void SetTrackName(FName InName) { TrackName = InName; }

private:

	/** Control Rig we control*/
	UPROPERTY()
	UControlRig* ControlRig;

	/** Section we should Key */
	UPROPERTY()
	UMovieSceneSection* SectionToKey;

	/** The sections owned by this track .*/
	UPROPERTY()
	TArray<UMovieSceneSection*> Sections;

	/** Unique Name*/
	UPROPERTY()
	FName TrackName;
};



