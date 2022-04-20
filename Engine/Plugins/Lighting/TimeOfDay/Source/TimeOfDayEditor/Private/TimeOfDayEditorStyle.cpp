// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeOfDayEditorStyle.h"
#include "TimeOfDayEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

// This is to fix the issue that SlateStyleMacros like IMAGE_BRUSH look for RootToContentDir but StyleSet->RootToContentDir is how this style is set up
#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FTimeOfDayEditorStyle::StyleInstance = nullptr;

void FTimeOfDayEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FTimeOfDayEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FTimeOfDayEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("TimeOfDayStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef< FSlateStyleSet > FTimeOfDayEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("TimeOfDayStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("TimeOfDay")->GetBaseDir() / TEXT("Resources"));

	Style->Set("TimeOfDay.OpenTimeOfDayEditor", new IMAGE_BRUSH_SVG("LightBulb", Icon20x20));

	return Style;
}


void FTimeOfDayEditorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FTimeOfDayEditorStyle::Get()
{
	return *StyleInstance;
}
