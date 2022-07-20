// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "HAL/Platform.h"
#include "MovieSceneSection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "MovieSceneSectionChannelOverrideRegistry.generated.h"

class UMovieSceneChannelOverrideContainer;
class UMovieSceneEntitySystemLinker;
class UMovieScenePropertyTrack;
struct FFrameNumber;
struct FMovieSceneChannel;
struct FMovieSceneEntityComponentFieldBuilder;
struct FMovieSceneEvaluationFieldEntityMetaData;
template <typename ElementType> class TRange;
template <typename T> struct TObjectPtr;

namespace UE
{
namespace MovieScene
{
	struct FEntityImportParams;
	struct FImportedEntity;
}
}

/**
* This object contains a map of actual channel overrides, where each override is a channel identifier and a channel container.
*/
UCLASS()
class MOVIESCENE_API UMovieSceneSectionChannelOverrideRegistry : public UObject
{
	GENERATED_BODY()

	static constexpr int32 ImportEntityIDOffset = 10;

private:
	/** Map of channel overrides. */
	UPROPERTY()
	TMap<int32, TObjectPtr<UMovieSceneChannelOverrideContainer>> Overrides;

public:
	UMovieSceneSectionChannelOverrideRegistry();

	/** 
	* A way for all kinds of sections inform the sequencer editor what channels they have
	*/
	EMovieSceneChannelProxyType CacheChannelProxy();

	/** 
	* Add channel to the registry 
	* @param ChannelToOverride		The key indicates which channel to override
	* @param Data					The container that owns a overriden channel instanse
	*/
	void AddChannel(int32 ChannelToOverride, UMovieSceneChannelOverrideContainer* ChannelContainer);

	/**
	* Removes a channel from the registry
	* @param ChannelToOverride		The key indicates which channel to override
	*/
	void RemoveChannel(int32 ChannelToRemove);

	/**
	* Forward ImportEntityImpl calls to overriden channels
	*/
	void ImportEntityImpl(const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity);

	/**
	* Called when overridden channels should populate evaluation field
	*/
	bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder, UMovieSceneSection& OwnerSection);

	/**
	* From Channel Index to EntityID
	* @param ChannelIndex	Represents the channel index in Overrides. Should be from 0 to 9
	*/
	static int32 ToEntityID(int32 ChannelIndex) { return ChannelIndex + ImportEntityIDOffset; }

	/**
	* From EntityID to channel index
	* @param EntityID	Represents the EntityID.
	*/
	static int32 ToChannelIndex(int32 EntityID) { return EntityID - ImportEntityIDOffset; }

	/**
	* Returns if the channel is overriden
	* @param ChannelIndex	Index for the channel.
	* @return	If this channel is overriden
	*/
	bool IsOverriden(int32 ChannelIndex) const;

	/**
	* Returns the overriden map
	*/
	const TMap<int32, TObjectPtr<UMovieSceneChannelOverrideContainer>>& GetOverrides() const { return Overrides; }
};

namespace UE
{
namespace MovieScene 
{
inline bool IsChannelOverriden(const UMovieSceneSectionChannelOverrideRegistry* OverrideRegistry, int32 ChannelIndex)
{
	return OverrideRegistry && OverrideRegistry->IsOverriden(ChannelIndex);
}

template<typename ChannelType>
inline bool HasAnyDataImpl(const UMovieSceneSectionChannelOverrideRegistry* OverrideRegistry, int32 Index, const ChannelType& InChannel)
{
	return InChannel.HasAnyData() || IsChannelOverriden(OverrideRegistry, Index);
}

template<typename HeadChannelType, typename ...TailChannelTypes>
inline bool HasAnyDataImpl(const UMovieSceneSectionChannelOverrideRegistry* OverrideRegistry, int32 HeadIndex, const HeadChannelType& InHeadChannel, TailChannelTypes ...InTailChannels)
{
	if (InHeadChannel.HasAnyData() || IsChannelOverriden(OverrideRegistry, HeadIndex))
	{
		return true;
	}
	return HasAnyDataImpl(OverrideRegistry, HeadIndex + 1, InTailChannels...);
}

/**
* Returns if at least one channel in the InChannels has any data. An example is HasAnyData(OverrideRegistry, 0, RedCurve, GreenCurve, BlueCurve, AlphaCurve)
*/
template<typename ...ChannelTypes>
inline bool HasAnyData(const UMovieSceneSectionChannelOverrideRegistry* OverrideRegistry, int32 HeadIndex, ChannelTypes ...InChannels)
{
	return HasAnyDataImpl(OverrideRegistry, 0, InChannels...);
}
}
}