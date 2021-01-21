// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"
#include "EntitySystem/MovieSceneOverlappingEntityTracker.h"

#include "MovieSceneInterrogatedPropertyInstantiator.generated.h"

class UMovieSceneBlenderSystem;

/** Class responsible for resolving all property types registered with FBuiltInComponentTypes::PropertyRegistry */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneInterrogatedPropertyInstantiatorSystem
	: public UMovieSceneEntityInstantiatorSystem
{
public:

	using FMovieSceneEntityID = UE::MovieScene::FMovieSceneEntityID;
	using FFloatRecompositionResult = UE::MovieScene::TRecompositionResult<float>;

	GENERATED_BODY()

	UMovieSceneInterrogatedPropertyInstantiatorSystem(const FObjectInitializer& ObjInit);

	/**
	 * Recompose a value from the constituent parts specified in InQuery, taking into accounts the wieghtings of the specific channel defined by ChannelCompositeIndex.
	 * This is basically a single-channel version of RecomposeBlendFinal below.
	 *
	 * @param PropertyDefinition  The property that this float channel is bound to
	 * @param ChannelCompositeIndex  The index of the composite that this float channel represents, if it is part of a composite value (for instance when keying/recomposing Translation.Z)
	 * @param InQuery        The query defining the entities and object to recompose
	 * @param InCurrentValue The value of the property to recompose
	 * @return A result containing the recomposed value for each of the entities specified in InQuery
	 */
	FFloatRecompositionResult RecomposeBlendFloatChannel(const UE::MovieScene::FPropertyDefinition& PropertyDefinition, int32 ChannelCompositeIndex, const UE::MovieScene::FDecompositionQuery& InQuery, float InCurrentValue);

	/**
	 * Recompose a value from the constituent parts specified in InQuery, taking into account the weightings of each channel.
	 * For instance, if a property comprises 3 additive values (a:1, b:2, c:3), and we recompose 'a' with an InCurrentValue of 10, the result for a would be 5.
	 *
	 * @param InComponents   The components that define the property to decompose
	 * @param InQuery        The query defining the entities and object to recompose
	 * @param InCurrentValue The value of the property to recompose
	 * @return A result matching the property type of the components, containing recomposed values for each of the entities specified in InQuery
	 */
	template<typename PropertyType, typename OperationalType>
	UE::MovieScene::TRecompositionResult<PropertyType> RecomposeBlendFinal(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& InComponents, const UE::MovieScene::FDecompositionQuery& InQuery, const PropertyType& InCurrentValue);

	/**
	 * Variant of RecomposeBlendFinal that returns the operational value type instead of the actual property type
	 */
	template<typename PropertyType, typename OperationalType>
	UE::MovieScene::TRecompositionResult<OperationalType> RecomposeBlendOperational(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& InComponents, const UE::MovieScene::FDecompositionQuery& InQuery, const OperationalType& InCurrentValue);

public:

	struct FPropertyInfo
	{
		FPropertyInfo()
			: BlendChannel(INVALID_BLEND_CHANNEL)
		{}
		/** POinter to the blender system to use for this property, if its blended */
		TWeakObjectPtr<UMovieSceneBlenderSystem> Blender;
		UE::MovieScene::FInterrogationChannel InterrogationChannel;
		/** The entity that contains the property component itself. For fast path properties this is the actual child entity produced from the bound object instantiators. */
		UE::MovieScene::FMovieSceneEntityID PropertyEntityID;
		/** Blend channel allocated from Blender, or INVALID_BLEND_CHANNEL if unblended. */
		uint16 BlendChannel;
	};

	// TOverlappingEntityTracker handler interface
	void InitializeOutput(UE::MovieScene::FInterrogationKey Key, TArrayView<const FMovieSceneEntityID> Inputs, FPropertyInfo* Output, UE::MovieScene::FEntityOutputAggregate Aggregate);
	void UpdateOutput(UE::MovieScene::FInterrogationKey Key, TArrayView<const FMovieSceneEntityID> Inputs, FPropertyInfo* Output, UE::MovieScene::FEntityOutputAggregate Aggregate);
	void DestroyOutput(UE::MovieScene::FInterrogationKey Key, FPropertyInfo* Output, UE::MovieScene::FEntityOutputAggregate Aggregate);

	const FPropertyInfo* FindPropertyInfo(UE::MovieScene::FInterrogationKey Key) const
	{
		return PropertyTracker.FindOutput(Key);
	}

	void FindEntityIDs(UE::MovieScene::FInterrogationKey Key, TArray<FMovieSceneEntityID>& OutEntityIDs) const
	{
		PropertyTracker.FindEntityIDs(Key, OutEntityIDs);
	}

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;

	bool PropertySupportsFastPath(TArrayView<const FMovieSceneEntityID> Inputs, FPropertyInfo* Output) const;
	UClass* ResolveBlenderClass(TArrayView<const FMovieSceneEntityID> Inputs) const;
	UE::MovieScene::FPropertyRecomposerPropertyInfo FindPropertyFromSource(FMovieSceneEntityID EntityID, UObject* Object) const;

private:

	static constexpr uint16 INVALID_BLEND_CHANNEL = uint16(-1);

	UE::MovieScene::TOverlappingEntityTracker<UE::MovieScene::FInterrogationKey, FPropertyInfo> PropertyTracker;
	UE::MovieScene::FComponentMask CleanFastPathMask;
	UE::MovieScene::FBuiltInComponentTypes* BuiltInComponents;
	UE::MovieScene::FPropertyRecomposerImpl RecomposerImpl;
};

template<typename PropertyType, typename OperationalType>
UE::MovieScene::TRecompositionResult<PropertyType> UMovieSceneInterrogatedPropertyInstantiatorSystem::RecomposeBlendFinal(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& Components, const UE::MovieScene::FDecompositionQuery& InQuery, const PropertyType& InCurrentValue)
{
	return RecomposerImpl.RecomposeBlendFinal<PropertyType, OperationalType>(Components, InQuery, InCurrentValue);
}

template<typename PropertyType, typename OperationalType>
UE::MovieScene::TRecompositionResult<OperationalType> UMovieSceneInterrogatedPropertyInstantiatorSystem::RecomposeBlendOperational(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& Components, const UE::MovieScene::FDecompositionQuery& InQuery, const OperationalType& InCurrentValue)
{
	return RecomposerImpl.RecomposeBlendOperational<PropertyType, OperationalType>(Components, InQuery, InCurrentValue);
}
