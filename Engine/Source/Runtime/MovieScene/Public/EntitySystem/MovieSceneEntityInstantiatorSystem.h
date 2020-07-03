// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "MovieSceneEntityInstantiatorSystem.generated.h"

struct FMovieSceneObjectBindingID;

class UMovieSceneEntitySystem;
class UMovieSceneEntitySystemLinker;

UCLASS(Abstract)
class MOVIESCENE_API UMovieSceneEntityInstantiatorSystem : public UMovieSceneEntitySystem
{
public:
	GENERATED_BODY()

	UMovieSceneEntityInstantiatorSystem(const FObjectInitializer& ObjInit);

	void UnlinkStaleObjectBindings(UE::MovieScene::TComponentTypeID<FGuid> BindingType);
	void UnlinkStaleObjectBindings(UE::MovieScene::TComponentTypeID<FMovieSceneObjectBindingID> BindingType);
};
