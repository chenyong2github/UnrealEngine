// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "MovieSceneSection.h"
#include "MovieSceneChannelOverrideContainer.generated.h"

class UMovieSceneEntitySystemLinker;
struct FMovieSceneEntityComponentFieldBuilder;
struct FMovieSceneChannel;

/**
* A wrapper to implement polymorphism for FMovieSceneChannel. Meant to be override by children classes
*/
UCLASS(Abstract)
class MOVIESCENE_API UMovieSceneChannelOverrideContainer : public UObject
{
	GENERATED_BODY()

public:
	/** Virtual methods to be overriden by children containers */
	virtual void ImportEntityImpl(int32 ChannelIndex, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity) {};
	virtual EMovieSceneChannelProxyType CacheChannelProxy() { return EMovieSceneChannelProxyType(); }

	virtual const FMovieSceneChannel* GetChannel() const { return nullptr; }
	virtual FMovieSceneChannel* GetChannel() { return nullptr; }
};