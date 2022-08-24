// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "MovieSceneSection.h"
#include "MovieSceneSignedObject.h"
#include "Misc/InlineValue.h"

#include "MovieSceneChannelOverrideContainer.generated.h"

class UMovieSceneEntitySystemLinker;
struct FMovieSceneChannel;
struct FMovieSceneChannelMetaData;
struct FMovieSceneChannelProxyData;
struct FMovieSceneEntityComponentFieldBuilder;

/**
 * Base structure for entity import parameters of a channel override.
 */
struct IMovieSceneChannelOverrideEntityImportParams
{
	FName ChannelName;
};

/*
 * Handle for storing channel override import parameters.
 *
 * These parameters carry type-specific information, such as the result component in which the channel
 * override should throw output values.
 */
struct FMovieSceneChannelOverrideEntityImportParamsHandle : 
	TInlineValue<IMovieSceneChannelOverrideEntityImportParams, sizeof(FName) + 16>
{
	using Super = TInlineValue<IMovieSceneChannelOverrideEntityImportParams, sizeof(FName) + 16>;

	FMovieSceneChannelOverrideEntityImportParamsHandle()
		: Super()
	{
	}

	template<typename T>
	FMovieSceneChannelOverrideEntityImportParamsHandle(T&& In)
		: Super(In)
	{
	}

	template<typename T>
	const T& CastThis() const
	{
		return *static_cast<const T*>(this->GetPtr());
	}
};

/**
 * Entity import parameters for channel overrides that just need an untyped result component.
 */
struct FMovieSceneChannelOverrideResultComponentEntityImportParams : IMovieSceneChannelOverrideEntityImportParams
{
	UE::MovieScene::FComponentTypeID ResultComponent;
};

/**
 * Entity import parameters for channel overrides that need a typed result component.
 */
template<typename ComponentType>
struct TMovieSceneChannelOverrideResultComponentEntityImportParams : IMovieSceneChannelOverrideEntityImportParams
{
	UE::MovieScene::TComponentTypeID<ComponentType> ResultComponent;
};

/**
 * A wrapper to implement polymorphism for FMovieSceneChannel.
 */
UCLASS(Abstract)
class MOVIESCENE_API UMovieSceneChannelOverrideContainer : public UMovieSceneSignedObject
{
	GENERATED_BODY()

public:

	/** Returns whether this container's underlying channel can be used as an override to the given channel type */
	virtual bool SupportsOverride(FName DefaultChannelTypeName) const { return false; }

	/** Imports the entity for this channel */
	virtual void ImportEntityImpl(
			const FMovieSceneChannelOverrideEntityImportParamsHandle& OverrideParams, 
			const UE::MovieScene::FEntityImportParams& ImportParams, UE::MovieScene::FImportedEntity* OutImportedEntity) {};

	/** Gets the underlying channel */
	virtual const FMovieSceneChannel* GetChannel() const { return nullptr; }
	/** Gets the underlying channel */
	virtual FMovieSceneChannel* GetChannel() { return nullptr; }

#if WITH_EDITOR
	/** Caches the channel proxy for this channel */
	virtual FMovieSceneChannelHandle AddChannelProxy(FName ChannelName, FMovieSceneChannelProxyData& ProxyData, const FMovieSceneChannelMetaData& MetaData) { return FMovieSceneChannelHandle(); }
#else
	virtual void AddChannelProxy(FName ChannelName, FMovieSceneChannelProxyData& ProxyData) {}
#endif

public:

	using FOverrideCandidates = TArray<TSubclassOf<UMovieSceneChannelOverrideContainer>, TInlineAllocator<8>>;

	/** Get a list of channel overrides that can work in the place of the given channel type */
	static void GetOverrideCandidates(FName InDefaultChannelTypeName, FOverrideCandidates& OutCandidates);
};

