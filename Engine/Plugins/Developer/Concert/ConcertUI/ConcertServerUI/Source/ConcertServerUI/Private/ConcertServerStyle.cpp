// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerStyle.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FConcertServerStyle::StyleInstance = nullptr;

void FConcertServerStyle::Initialize()
{
	// The core style must be initialized before the editor style
	//FSlateApplication::InitializeCoreStyle();
	//FEditorStyle::ResetToDefault();

	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FConcertServerStyle::Shutdown()
{
	if (StyleInstance)
	{
		
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

const ISlateStyle& FConcertServerStyle::Get()
{
	return *StyleInstance;
}

FName FConcertServerStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("ConcertServerStyle"));
	return StyleSetName;
}

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FConcertServerStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define IMAGE_PLUGIN_BRUSH_SVG( RelativePath, ... ) FSlateVectorImageBrush( FConcertServerStyle::InContent(RelativePath, ".svg"), __VA_ARGS__)

FString FConcertServerStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("ConcertServerUI"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

TSharedRef<FSlateStyleSet> FConcertServerStyle::Create()
{
	TSharedRef<FSlateStyleSet> StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Slate/Starship/Insights"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));
	
	const FVector2D Icon12x12(12.0f, 12.0f); 
	const FVector2D Icon16x16(16.0f, 16.0f); 
	const FVector2D Icon20x20(20.0f, 20.0f); 
	const FVector2D Icon32x32(20.0f, 20.0f); 
	
	StyleSet->Set("Concert.MultiUser", new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUser_32x", Icon32x32));
	return StyleSet;
}

#undef TODO_IMAGE_BRUSH
#undef EDITOR_BOX_BRUSH
#undef EDITOR_IMAGE_BRUSH_SVG
#undef EDITOR_IMAGE_BRUSH

