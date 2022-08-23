// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectMixerEditorStyle.h"

#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

TSharedPtr<FSlateStyleSet> FObjectMixerEditorStyle::StyleInstance = nullptr;

void FObjectMixerEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FObjectMixerEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

void FObjectMixerEditorStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

const ISlateStyle& FObjectMixerEditorStyle::Get()
{
	return *StyleInstance;
}

const FName& FObjectMixerEditorStyle::GetStyleSetName() const
{
	static FName StyleName(TEXT("ObjectMixerEditor"));
	return StyleName;
}

FString GetConcertContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("ConcertSyncClient"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

const FSlateBrush* FObjectMixerEditorStyle::GetBrush(const FName PropertyName, const ANSICHAR* Specifier, const ISlateStyle* RequestingStyle) const
{
	return StyleInstance->GetBrush(PropertyName, Specifier);
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define IMAGE_BRUSH_SVG( RelativePath, ... ) FSlateVectorImageBrush( Style->RootToContentDir(RelativePath, TEXT(".svg") ), __VA_ARGS__)
#define IMAGE_PLUGIN_BRUSH_SVG( PluginName, RelativePath, ... ) FSlateVectorImageBrush( FObjectMixerEditorStyle::GetExternalPluginContent(PluginName, RelativePath, ".svg"), __VA_ARGS__)

const FVector2D Icon64x64(64.f, 64.f);
const FVector2D Icon40x40(40.0f, 40.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon12x12(12.0f, 12.0f);
const FVector2D Icon8x8(8.f, 8.f);

FString FObjectMixerEditorStyle::GetExternalPluginContent(const FString& PluginName, const FString& RelativePath, const ANSICHAR* Extension)
{
	FString ContentDir = IPluginManager::Get().FindPlugin(PluginName)->GetBaseDir() / RelativePath + Extension;
	return ContentDir;
}

TSharedRef< FSlateStyleSet > FObjectMixerEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>("ObjectMixerEditor");
	
	//Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));

	//Style->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	// Icons
	Style->Set("ObjectMixer.ToolbarButton", new IMAGE_BRUSH_SVG("Icons/ObjectMixer", Icon40x40));
	Style->Set("ObjectMixer.ToolbarButton.Small", new IMAGE_BRUSH_SVG("Icons/ObjectMixer", Icon20x20));

	// Brush
	Style->Set("ObjectMixerEditor.BrightBorder", new FSlateColorBrush(FStyleColors::Header));

	// Border colors for Results view
	Style->Set("ObjectMixerEditor.HeaderRowBorder", new FSlateColorBrush(FStyleColors::Black));
	Style->Set("ObjectMixerEditor.DefaultBorder", new FSlateColorBrush(FStyleColors::Transparent));
	
	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef IMAGE_BRUSH_SVG
#undef IMAGE_PLUGIN_BRUSH_SVG
