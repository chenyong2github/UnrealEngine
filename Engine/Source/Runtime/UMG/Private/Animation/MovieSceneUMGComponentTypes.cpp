// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/MovieSceneUMGComponentTypes.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "MovieSceneTracksComponentTypes.h"

#include "Components/Widget.h"

namespace UE
{
namespace MovieScene
{

static bool GMovieSceneUMGComponentTypesDestroyed = false;
static TUniquePtr<FMovieSceneUMGComponentTypes> GMovieSceneUMGComponentTypes;


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

void ConvertOperationalProperty(const FIntermediateWidgetTransform& In, FWidgetTransform& Out)
{
	Out.Translation.X = In.TranslationX;
	Out.Translation.Y = In.TranslationY;
	Out.Angle = In.Rotation;
	Out.Scale.X = In.ScaleX;
	Out.Scale.Y = In.ScaleY;
	Out.Shear.X = In.ShearX;
	Out.Shear.Y = In.ShearY;
}
void ConvertOperationalProperty(const FWidgetTransform& In, FIntermediateWidgetTransform& Out)
{
	Out.TranslationX = In.Translation.X;
	Out.TranslationY = In.Translation.Y;
	Out.Rotation = In.Angle;
	Out.ScaleX = In.Scale.X;
	Out.ScaleY = In.Scale.Y;
	Out.ShearX = In.Shear.X;
	Out.ShearY = In.Shear.Y;
}

static float GetRenderOpacity(const UObject* Object)
{
	return CastChecked<const UWidget>(Object)->GetRenderOpacity();
}

static void SetRenderOpacity(UObject* Object, float InRenderOpacity)
{
	CastChecked<UWidget>(Object)->SetRenderOpacity(InRenderOpacity);
}

static FWidgetTransform GetRenderTransform(const UObject* Object)
{
	return CastChecked<const UWidget>(Object)->RenderTransform;
}

static void SetRenderTransform(UObject* Object, const FWidgetTransform& InRenderTransform)
{
	CastChecked<UWidget>(Object)->SetRenderTransform(InRenderTransform);
}

FMovieSceneUMGComponentTypes::FMovieSceneUMGComponentTypes()
{
	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	WidgetTransform.PropertyTag = ComponentRegistry->NewTag(TEXT("FWidgetTransform Property"),  EComponentTypeFlags::CopyToChildren);
	ComponentRegistry->NewComponentType(&WidgetTransform.PreAnimatedValue, TEXT("Pre-Animated 2D Transform"),     EComponentTypeFlags::Preserved);
	ComponentRegistry->NewComponentType(&WidgetTransform.InitialValue,     TEXT("Initial 2D Transform"),          EComponentTypeFlags::Preserved);

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	FMovieSceneTracksComponentTypes::Get()->Accessors.Float.Add(UWidget::StaticClass(), "RenderOpacity", &GetRenderOpacity, &SetRenderOpacity);

	CustomWidgetTransformAccessors.Add(UWidget::StaticClass(), "RenderTransform", &GetRenderTransform, &SetRenderTransform);

	BuiltInComponents->PropertyRegistry.DefineCompositeProperty(WidgetTransform)
	.AddComposite<&FIntermediateWidgetTransform::TranslationX>(BuiltInComponents->FloatResult[0])
	.AddComposite<&FIntermediateWidgetTransform::TranslationY>(BuiltInComponents->FloatResult[1])
	.AddComposite<&FIntermediateWidgetTransform::Rotation>(BuiltInComponents->FloatResult[2])
	.AddComposite<&FIntermediateWidgetTransform::ScaleX>(BuiltInComponents->FloatResult[3])
	.AddComposite<&FIntermediateWidgetTransform::ScaleY>(BuiltInComponents->FloatResult[4])
	.AddComposite<&FIntermediateWidgetTransform::ShearX>(BuiltInComponents->FloatResult[5])
	.AddComposite<&FIntermediateWidgetTransform::ShearY>(BuiltInComponents->FloatResult[6])
	.SetCustomAccessors(&CustomWidgetTransformAccessors)
	.Commit();
}

FMovieSceneUMGComponentTypes::~FMovieSceneUMGComponentTypes()
{
}

void FMovieSceneUMGComponentTypes::Destroy()
{
	GMovieSceneUMGComponentTypes.Reset();
	GMovieSceneUMGComponentTypesDestroyed = true;
}

FMovieSceneUMGComponentTypes* FMovieSceneUMGComponentTypes::Get()
{
	if (!GMovieSceneUMGComponentTypes.IsValid())
	{
		check(!GMovieSceneUMGComponentTypesDestroyed);
		GMovieSceneUMGComponentTypes.Reset(new FMovieSceneUMGComponentTypes);
	}
	return GMovieSceneUMGComponentTypes.Get();
}


} // namespace MovieScene
} // namespace UE
