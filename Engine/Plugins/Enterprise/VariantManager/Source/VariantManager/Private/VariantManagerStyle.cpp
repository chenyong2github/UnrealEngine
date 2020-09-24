// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariantManagerStyle.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FVariantManagerStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

FString FVariantManagerStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("VariantManager"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

TSharedPtr< FSlateStyleSet > FVariantManagerStyle::StyleSet = nullptr;
TSharedPtr< class ISlateStyle > FVariantManagerStyle::Get() { return StyleSet; }

FName FVariantManagerStyle::GetStyleSetName()
{
	static FName PaperStyleName(TEXT("VariantManager"));
	return PaperStyleName;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void FVariantManagerStyle::Initialize()
{
	// Const icon sizes
	const FVector2D Icon8x8(8.0f, 8.0f);
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	// Only register once
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	{
		/** Color used for the background of the entire variant manager as well as the spacer border */
		StyleSet->Set( "VariantManager.Panels.LightBackgroundColor", FLinearColor( FColor( 96, 96, 96, 255 ) ) );

		/** Color used as background for variant nodes, and background of properties and dependencies panels */
		StyleSet->Set( "VariantManager.Panels.ContentBackgroundColor", FLinearColor( FColor( 62, 62, 62, 255 ) ) );

		/** Color used for background of variant set nodes and panel headers, like Properties or Dependencies headers */
		StyleSet->Set( "VariantManager.Panels.HeaderBackgroundColor", FLinearColor( FColor( 48, 48, 48, 255 ) ) );

		/** Thickness of the light border around the entire variant manager tab and between items */
		StyleSet->Set( "VariantManager.Spacings.BorderThickness", 4.0f );

		/** The amount to indent child nodes of the layout tree */
		StyleSet->Set( "VariantManager.Spacings.IndentAmount", 10.0f );
	}

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
};

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef IMAGE_PLUGIN_BRUSH
#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef DEFAULT_FONT

void FVariantManagerStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}
