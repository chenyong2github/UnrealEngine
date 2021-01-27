// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "EntitySystem/MovieScenePropertyBinding.h"
#include "MovieScenePropertyTrack.generated.h"

/**
 * Base class for tracks that animate an object property
 */
UCLASS(abstract)
class MOVIESCENETRACKS_API UMovieScenePropertyTrack
	: public UMovieSceneNameableTrack
{
	GENERATED_UCLASS_BODY()

public:

	// UMovieSceneTrack interface

	virtual void RemoveAllAnimationData() override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool IsEmpty() const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
	virtual bool CanRename() const override { return false; }
	virtual FName GetTrackName() const override;
#endif

	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;

public:

	/**
	 * Sets the property name for this animatable property
	 *
	 * @param InPropertyName The property being animated
	 */
	void SetPropertyNameAndPath(FName InPropertyName, const FString& InPropertyPath);

	/** @return the name of the property being animated by this track */
	FName GetPropertyName() const { return PropertyBinding.PropertyName; }

	/** @return The property path for this track */
	FName GetPropertyPath() const { return PropertyBinding.PropertyPath; }

	/** Access the property binding for this track */
	const FMovieScenePropertyBinding& GetPropertyBinding() const { return PropertyBinding; }

	template <typename ValueType>
	TOptional<ValueType> GetCurrentValue(const UObject* Object) const
	{
		return FTrackInstancePropertyBindings::StaticValue<ValueType>(Object, PropertyBinding.PropertyPath.ToString());
	}

	/**
	* Find all sections at the current time.
	*
	*@param Time  The Time relative to the owning movie scene where the section should be
	*@Return All sections at that time
	*/
	TArray<UMovieSceneSection*, TInlineAllocator<4>> FindAllSections(FFrameNumber Time);

	/**
	 * Finds a section at the current time.
	 *
	 * @param Time The time relative to the owning movie scene where the section should be
	 * @return The found section.
	 */
	class UMovieSceneSection* FindSection(FFrameNumber Time);

	/**
	 * Finds a section at the current time or extends an existing one
	 *
	 * @param Time The time relative to the owning movie scene where the section should be
	 * @param OutWeight The weight of the section if found
	 * @return The found section.
	 */
	class UMovieSceneSection* FindOrExtendSection(FFrameNumber Time, float& OutWeight);

	/**
	 * Finds a section at the current time.
	 *
	 * @param Time The time relative to the owning movie scene where the section should be
	 * @param bSectionAdded Whether a section was added or not
	 * @return The found section, or the new section.
	 */
	class UMovieSceneSection* FindOrAddSection(FFrameNumber Time, bool& bSectionAdded);

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

#if WITH_EDITORONLY_DATA
public:
	/** Unique name for this track to afford multiple tracks on a given object (i.e. for array properties) */
	UPROPERTY()
	FName UniqueTrackName;

	/** Name of the property being changed */
	UPROPERTY()
	FName PropertyName_DEPRECATED;

	/** Path to the property from the source object being changed */
	UPROPERTY()
	FString PropertyPath_DEPRECATED;

#endif


private:
	/** Section we should Key */
	UPROPERTY()
	TObjectPtr<UMovieSceneSection> SectionToKey;

protected:

	UPROPERTY()
	FMovieScenePropertyBinding PropertyBinding;

	/** All the sections in this list */
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
