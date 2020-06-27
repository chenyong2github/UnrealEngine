// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Slate/WidgetTransform.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"


namespace UE
{
namespace MovieScene
{

struct FIntermediateWidgetTransform;

struct UMG_API FMovieSceneUMGComponentTypes
{
	~FMovieSceneUMGComponentTypes();

	TPropertyComponents<FWidgetTransform, FIntermediateWidgetTransform> WidgetTransform;
	TCustomPropertyRegistration<FWidgetTransform, 1> CustomWidgetTransformAccessors;

	static void Destroy();

	static FMovieSceneUMGComponentTypes* Get();

private:
	FMovieSceneUMGComponentTypes();
};


} // namespace MovieScene
} // namespace UE
