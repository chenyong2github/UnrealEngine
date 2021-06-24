// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Systems/MovieScenePiecewiseBoolBlenderSystem.h"
#include "EntitySystem/MovieScenePropertyBinding.h"
#include "UObject/ObjectMacros.h"
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
	UMovieSceneSection* SectionToKey;

protected:

	UPROPERTY()
	FMovieScenePropertyBinding PropertyBinding;

	/** All the sections in this list */
	UPROPERTY()
	TArray<UMovieSceneSection*> Sections;
};


struct MOVIESCENETRACKS_API FMovieScenePropertyTrackEntityImportHelper
{
	static const int32 SectionPropertyValueImportingID;
	static const int32 SectionEditConditionToggleImportingID;

	static void PopulateEvaluationField(UMovieSceneSection& Section, const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder);

	static bool IsPropertyValueID(const UE::MovieScene::FEntityImportParams& Params);
	static bool IsEditConditionToggleID(const UE::MovieScene::FEntityImportParams& Params);
	static void ImportEditConditionToggleEntity(const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity);

	static FName SanitizeBoolPropertyName(FName InPropertyName);
};


namespace UE
{
namespace MovieScene
{

/**
 * Utility class for importing a customizable property track entity in a way that automatically supports
 * being inside a bound property track or not, and being hooked up to a property with an edit condition or not.
 */
template<typename... T>
struct TPropertyTrackEntityImportHelperImpl
{
	TPropertyTrackEntityImportHelperImpl(TEntityBuilder<T...>&& InBuilder)
		: Builder(MoveTemp(InBuilder))
	{
	}

	template<typename U, typename PayloadType>
	TPropertyTrackEntityImportHelperImpl<T..., TAdd<U>> Add(TComponentTypeID<U> ComponentType, PayloadType&& InPayload)
	{
		return TPropertyTrackEntityImportHelperImpl<T..., TAdd<U>>(Builder.Add(ComponentType, InPayload));
	}

	template<typename U, typename PayloadType>
	TPropertyTrackEntityImportHelperImpl<T..., TAddConditional<U>> AddConditional(TComponentTypeID<U> ComponentType, PayloadType&& InPayload, bool bCondition)
	{
		return TPropertyTrackEntityImportHelperImpl<T..., TAddConditional<U>>(Builder.AddConditional(ComponentType, InPayload, bCondition));
	}

	void Commit(const UMovieSceneSection* InSection, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity)
	{
		const FGuid ObjectBindingID = Params.GetObjectBindingID();

		const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
		const FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

		if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(InSection->GetOuter()))
		{
			if (FMovieScenePropertyTrackEntityImportHelper::IsPropertyValueID(Params))
			{
				OutImportedEntity->AddBuilder(
					Builder
						.Add(BuiltInComponents->PropertyBinding, PropertyTrack->GetPropertyBinding())
						.AddConditional(BuiltInComponents->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid()));
			}
			else if (FMovieScenePropertyTrackEntityImportHelper::IsEditConditionToggleID(Params))
			{
				// We effectively discard the builder we've been setting up, because we just
				// need to import the edit condition toggle entity.
				FMovieScenePropertyTrackEntityImportHelper::ImportEditConditionToggleEntity(Params, OutImportedEntity);
			}
		}
		else
		{
			OutImportedEntity->AddBuilder(
				Builder
					.AddConditional(BuiltInComponents->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid()));
		}
	}

protected:

	TEntityBuilder<T...> Builder;
};


/**
 * The starting point for TPropertyTrackEntityImportHelperImpl<...T>
 */
template<>
struct TPropertyTrackEntityImportHelperImpl<>
{
	TPropertyTrackEntityImportHelperImpl(FComponentTypeID PropertyTag)
		: Builder(FEntityBuilder().AddTag(PropertyTag))
	{
	}

	template<typename U, typename PayloadType>
	TPropertyTrackEntityImportHelperImpl<FAdd, TAdd<U>> Add(TComponentTypeID<U> ComponentType, PayloadType&& InPayload)
	{
		return TPropertyTrackEntityImportHelperImpl<FAdd, TAdd<U>>(Builder.Add(ComponentType, InPayload));
	}

	template<typename U, typename PayloadType>
	TPropertyTrackEntityImportHelperImpl<FAdd, TAddConditional<U>> AddConditional(TComponentTypeID<U> ComponentType, PayloadType&& InPayload, bool bCondition)
	{
		return TPropertyTrackEntityImportHelperImpl<FAdd, TAddConditional<U>>(Builder.AddConditional(ComponentType, InPayload, bCondition));
	}

protected:

	TEntityBuilder<FAdd> Builder;
};

struct MOVIESCENETRACKS_API FPropertyTrackEntityImportHelper : TPropertyTrackEntityImportHelperImpl<>
{
	template<typename PropertyTraits>
	FPropertyTrackEntityImportHelper(const TPropertyComponents<PropertyTraits>& PropertyComponents)
		: TPropertyTrackEntityImportHelperImpl<>(PropertyComponents.PropertyTag)
	{}
};

}
}

