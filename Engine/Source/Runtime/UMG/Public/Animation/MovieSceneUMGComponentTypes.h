// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Layout/Margin.h"
#include "Slate/WidgetTransform.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieScenePropertyTraits.h"
#include "EntitySystem/MovieScenePropertyMetaDataTraits.h"

#include "Containers/ArrayView.h"


namespace UE
{
namespace MovieScene
{

struct FWidgetMaterialPath
{
	FWidgetMaterialPath() = default;
	FWidgetMaterialPath(TArrayView<const FName> Names)
		: Path(Names.GetData(), Names.Num())
	{}

	TArray<FName, TInlineAllocator<2>> Path;
};

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

using FMarginTraits = TDirectPropertyTraits<FMargin>;
using FWidgetTransformPropertyTraits = TIndirectPropertyTraits<FWidgetTransform, FIntermediateWidgetTransform>;

struct UMG_API FMovieSceneUMGComponentTypes
{
	~FMovieSceneUMGComponentTypes();

	TPropertyComponents<FMarginTraits> Margin;
	TPropertyComponents<FWidgetTransformPropertyTraits> WidgetTransform;

	TComponentTypeID<FWidgetMaterialPath> WidgetMaterialPath;

	TCustomPropertyRegistration<FWidgetTransformPropertyTraits, 1> CustomWidgetTransformAccessors;

	static void Destroy();

	static FMovieSceneUMGComponentTypes* Get();

private:
	FMovieSceneUMGComponentTypes();
};


} // namespace MovieScene
} // namespace UE
