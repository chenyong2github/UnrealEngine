// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/LensDistortionEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace LensDistortionEditorStyle
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);

	const FName NAME_StyleName(TEXT("LensDistortionStyle"));

	static TUniquePtr<FSlateStyleSet> StyleInstance;
}

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(LensDistortionEditorStyle::StyleInstance->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

void FLensDistortionEditorStyle::Register()
{
	LensDistortionEditorStyle::StyleInstance = MakeUnique<FSlateStyleSet>(LensDistortionEditorStyle::NAME_StyleName);
	LensDistortionEditorStyle::StyleInstance->SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Compositing/LensDistortion/Content/Editor/Icons/"));

	LensDistortionEditorStyle::StyleInstance->Set("ClassThumbnail.LensFile", new IMAGE_BRUSH("LensFileIcon_64x", LensDistortionEditorStyle::Icon64x64));
	LensDistortionEditorStyle::StyleInstance->Set("ClassIcon.LensFile", new IMAGE_BRUSH("LensFileIcon_20x", LensDistortionEditorStyle::Icon20x20));


	FSlateStyleRegistry::RegisterSlateStyle(*LensDistortionEditorStyle::StyleInstance.Get());
}

void FLensDistortionEditorStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*LensDistortionEditorStyle::StyleInstance.Get());
	LensDistortionEditorStyle::StyleInstance.Reset();
}

FName FLensDistortionEditorStyle::GetStyleSetName()
{
	return LensDistortionEditorStyle::NAME_StyleName;
}

const ISlateStyle& FLensDistortionEditorStyle::Get()
{
	check(LensDistortionEditorStyle::StyleInstance.IsValid());
	return *LensDistortionEditorStyle::StyleInstance.Get();
}

#undef IMAGE_BRUSH
