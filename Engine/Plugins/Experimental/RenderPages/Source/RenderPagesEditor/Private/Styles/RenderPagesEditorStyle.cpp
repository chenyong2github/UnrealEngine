// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styles/RenderPagesEditorStyle.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"


TSharedPtr<FSlateStyleSet> UE::RenderPages::Private::FRenderPagesEditorStyle::StyleInstance = nullptr;


const ISlateStyle& UE::RenderPages::Private::FRenderPagesEditorStyle::Get()
{
	return *StyleInstance;
}

void UE::RenderPages::Private::FRenderPagesEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void UE::RenderPages::Private::FRenderPagesEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName UE::RenderPages::Private::FRenderPagesEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("LevelSnapshotsEditor"));
	return StyleSetName;
}

const FLinearColor& UE::RenderPages::Private::FRenderPagesEditorStyle::GetColor(FName PropertyName, const ANSICHAR* Specifier)
{
	return StyleInstance->GetColor(PropertyName, Specifier);
}


const FSlateBrush* UE::RenderPages::Private::FRenderPagesEditorStyle::GetBrush(FName PropertyName, const ANSICHAR* Specifier)
{
	return StyleInstance->GetBrush(PropertyName, Specifier);
}

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define IMAGE_BRUSH_SVG(RelativePath, ...) FSlateVectorImageBrush(Style->RootToContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)

const FVector2D Icon64x64(64.f, 64.f);
const FVector2D Icon40x40(40.0f, 40.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon12x12(12.0f, 12.0f);

TSharedRef<FSlateStyleSet> UE::RenderPages::Private::FRenderPagesEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>("RenderPagesEditor");

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RenderPages"));
	check(Plugin.IsValid());
	if (Plugin.IsValid())
	{
		Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	}

	Style->Set("Invisible",
		FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetNormalPadding(FMargin())
		.SetPressedPadding(FMargin())
	);
	Style->Set("HoverHintOnly",
		FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetHovered(BOX_BRUSH("Common/ButtonHoverHint", FMargin(4/16.0f), FLinearColor(1,1,1,0.05f)))
		.SetPressed(BOX_BRUSH("Common/ButtonHoverHint", FMargin(4/16.0f), FLinearColor(1,1,1,0.15f)))
		.SetNormalPadding(FMargin())
		.SetPressedPadding(FMargin())
	);

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef IMAGE_BRUSH_SVG

void UE::RenderPages::Private::FRenderPagesEditorStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}
