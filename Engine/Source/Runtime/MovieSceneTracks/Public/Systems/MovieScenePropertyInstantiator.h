// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"

#include "Misc/TVariant.h"
#include "Misc/Optional.h"
#include "Containers/BitArray.h"
#include "Containers/Map.h"
#include "Containers/SortedMap.h"

#include "MovieScenePropertyInstantiator.generated.h"

namespace UE
{
namespace MovieScene
{

struct FPropertyStats;
class FEntityManager;

} // namespace MovieScene
} // namespace UE

class UMovieSceneBlenderSystem;

/** Class responsible for resolving all property types registered with FBuiltInComponentTypes::PropertyRegistry */
UCLASS()
class MOVIESCENETRACKS_API UMovieScenePropertyInstantiatorSystem
	: public UMovieSceneEntityInstantiatorSystem
	, public IMovieScenePreAnimatedStateSystemInterface
{
public:

	using FMovieSceneEntityID = UE::MovieScene::FMovieSceneEntityID;

	GENERATED_BODY()

	UMovieScenePropertyInstantiatorSystem(const FObjectInitializer& ObjInit);

	/**
	 * Retrieve the stats for a specific property
	 */
	UE::MovieScene::FPropertyStats GetStatsForProperty(UE::MovieScene::FCompositePropertyTypeID PropertyID) const;


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

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual void OnLink() override;
	virtual void OnUnlink() override;

private:

	virtual void SavePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual void SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	virtual void RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:

	using FChannelMask     = TBitArray<TFixedAllocator< 1 >>;
	using FSlowPropertyPtr = TSharedPtr<FTrackInstancePropertyBindings>;

	struct FObjectPropertyInfo
	{
		FObjectPropertyInfo()
			: BoundObject(nullptr)
			, BlendChannel(INVALID_BLEND_CHANNEL)
			, bWantsRestoreState(false)
		{}

		/** Variant of the property itself as either a pointer offset, a custom property index, or slow track instance bindings object */
		TOptional< TVariant<uint16, UE::MovieScene::FCustomPropertyIndex, FSlowPropertyPtr> > Property;

		/** POinter to the blender system to use for this property, if its blended */
		TWeakObjectPtr<UMovieSceneBlenderSystem> Blender;
		/** The object being animated */
		UObject* BoundObject;
		/** The path of the property being animated */
		FName PropertyPath;
		/** Mask of composite channels that are not animated (set bits indicate an unanimated channel) */
		FChannelMask EmptyChannels;
		/** The entity that contains the property component itself. For fast path properties this is the actual child entity produced from the bound object instantiators. */
		UE::MovieScene::FMovieSceneEntityID PropertyEntityID;
		/** Blend channel allocated from Blender, or INVALID_BLEND_CHANNEL if unblended. */
		uint16 BlendChannel;
		/** The index of this property within FPropertyRegistry::GetProperties. */
		int32 PropertyDefinitionIndex;
		/** true if any of the contributors to this property need restore state.. */
		bool bWantsRestoreState;
	};

private:

	/* Parameter structure passed around when instantiating a specific instance of a property */
	struct FPropertyParameters
	{
		/** Pointer to the property instance to be animated */
		FObjectPropertyInfo* PropertyInfo;
		/** Pointer to the property type definition from FPropertyRegistry */
		const UE::MovieScene::FPropertyDefinition* PropertyDefinition;
		/** The index of the PropertyInfo member within UMovieScenePropertyInstantiatorSystem::ResolvedProperties */
		int32 PropertyInfoIndex;
	};
	void DiscoverInvalidatedProperties(TBitArray<>& OutInvalidatedProperties);
	void ProcessInvalidatedProperties(const TBitArray<>& InvalidatedProperties);
	void UpdatePropertyInfo(const FPropertyParameters& Params);
	bool PropertySupportsFastPath(const FPropertyParameters& Params) const;
	void InitializeFastPath(const FPropertyParameters& Params);
	void InitializeBlendPath(const FPropertyParameters& Params);
	int32 FindCustomAccessorIndex(UE::MovieScene::FCustomAccessorView Accessors, UClass* ClassType, FName PropertyPath);
	TOptional<uint16> ComputeFastPropertyPtrOffset(UClass* ObjectClass, const FMovieScenePropertyBinding& PropertyBinding);
	int32 ResolveProperty(UE::MovieScene::FCustomAccessorView CustomAccessors, UObject* Object, const FMovieScenePropertyBinding& PropertyBinding);

	const FObjectPropertyInfo* FindPropertyFromSource(FMovieSceneEntityID EntityID, UObject* Object) const;

	void AssignPreAnimatedValues(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);
	void RestorePreAnimatedValues(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);
	void AssignInitialValues(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);

	void CleanTaggedGarbage(UMovieSceneEntitySystemLinker*);

private:

	static constexpr uint16 INVALID_BLEND_CHANNEL = uint16(-1);

	TSparseArray<FObjectPropertyInfo> ResolvedProperties;
	TMultiMap<int32, FMovieSceneEntityID> Contributors;
	TMultiMap<int32, FMovieSceneEntityID> NewContributors;
	TMap<FMovieSceneEntityID, int32> EntityToProperty;
	TMap< TTuple<UObject*, FName>, int32 > ObjectPropertyToResolvedIndex;

	TArray<UE::MovieScene::FPropertyStats> PropertyStats;

	UE::MovieScene::FComponentMask CleanFastPathMask;

	TBitArray<> SaveGlobalStateTasks;
	TBitArray<> CachePreAnimatedStateTasks;
	TBitArray<> RestorePreAnimatedStateTasks;
	TBitArray<> InitialValueStateTasks;

	UE::MovieScene::FBuiltInComponentTypes* BuiltInComponents;
};



template<typename PropertyType, typename OperationalType>
UE::MovieScene::TRecompositionResult<PropertyType> UMovieScenePropertyInstantiatorSystem::RecomposeBlendFinal(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& Components, const UE::MovieScene::FDecompositionQuery& InQuery, const PropertyType& InCurrentValue)
{
	using namespace UE::MovieScene;

	const FPropertyDefinition& PropertyDefinition = BuiltInComponents->PropertyRegistry.GetDefinition(Components.CompositeID);

	TRecompositionResult<PropertyType> Result(InCurrentValue, InQuery.Entities.Num());

	if (InQuery.Entities.Num() == 0)
	{
		return Result;
	}

	const FObjectPropertyInfo* Property = FindPropertyFromSource(InQuery.Entities[0], InQuery.Object);

	if (!Property || Property->BlendChannel == INVALID_BLEND_CHANNEL)
	{
		return Result;
	}

	UMovieSceneBlenderSystem* Blender = Property->Blender.Get();
	if (!Blender)
	{
		return Result;
	}

	FFloatDecompositionParams Params;
	Params.Query = InQuery;
	Params.PropertyEntityID = Property->PropertyEntityID;
	Params.DecomposeBlendChannel = Property->BlendChannel;
	Params.PropertyTag = PropertyDefinition.PropertyType;

	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(PropertyDefinition);

	PropertyDefinition.Handler->RecomposeBlendFinal(PropertyDefinition, Composites, Params, Blender, InCurrentValue, Result.Values);

	return Result;
}

template<typename PropertyType, typename OperationalType>
UE::MovieScene::TRecompositionResult<OperationalType> UMovieScenePropertyInstantiatorSystem::RecomposeBlendOperational(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& Components, const UE::MovieScene::FDecompositionQuery& InQuery, const OperationalType& InCurrentValue)
{
	using namespace UE::MovieScene;

	const FPropertyDefinition& PropertyDefinition = BuiltInComponents->PropertyRegistry.GetDefinition(Components.CompositeID);

	TRecompositionResult<OperationalType> Result(InCurrentValue, InQuery.Entities.Num());

	if (InQuery.Entities.Num() == 0)
	{
		return Result;
	}

	const FObjectPropertyInfo* Property = FindPropertyFromSource(InQuery.Entities[0], InQuery.Object);

	if (!Property || Property->BlendChannel == INVALID_BLEND_CHANNEL)
	{
		return Result;
	}

	UMovieSceneBlenderSystem* Blender = Property->Blender.Get();
	if (!Blender)
	{
		return Result;
	}

	FFloatDecompositionParams Params;
	Params.Query = InQuery;
	Params.PropertyEntityID = Property->PropertyEntityID;
	Params.DecomposeBlendChannel = Property->BlendChannel;
	Params.PropertyTag = PropertyDefinition.PropertyType;

	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(PropertyDefinition);

	PropertyDefinition.Handler->RecomposeBlendOperational(PropertyDefinition, Composites, Params, Blender, InCurrentValue, Result.Values);

	return Result;
}

