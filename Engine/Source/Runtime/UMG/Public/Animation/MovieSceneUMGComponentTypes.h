// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Slate/WidgetTransform.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieScenePropertyTraits.h"
#include "EntitySystem/MovieScenePropertyMetaDataTraits.h"


namespace UE
{
namespace MovieScene
{

struct FIntermediateWidgetTransform
{
	float TranslationX;
	float TranslationY;
	float Rotation;
	float ScaleX;
	float ScaleY;
	float ShearX;
	float ShearY;
};
UMG_API void ConvertOperationalProperty(const FIntermediateWidgetTransform& In, FWidgetTransform& Out);
UMG_API void ConvertOperationalProperty(const FWidgetTransform& In, FIntermediateWidgetTransform& Out);

using FWidgetTransformPropertyTraits = TIndirectPropertyTraits<FWidgetTransform, FIntermediateWidgetTransform>;

struct UMG_API FMovieSceneUMGComponentTypes
{
	~FMovieSceneUMGComponentTypes();

	TPropertyComponents<FWidgetTransformPropertyTraits> WidgetTransform;
	TCustomPropertyRegistration<FWidgetTransformPropertyTraits, 1> CustomWidgetTransformAccessors;

	static void Destroy();

	static FMovieSceneUMGComponentTypes* Get();

private:
	FMovieSceneUMGComponentTypes();
};


} // namespace MovieScene
} // namespace UE
