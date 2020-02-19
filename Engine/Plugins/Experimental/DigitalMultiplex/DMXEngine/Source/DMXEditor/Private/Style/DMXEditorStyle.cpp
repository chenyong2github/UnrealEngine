// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorStyle.h"

#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/Font.h"

TSharedPtr<FSlateStyleSet> FDMXEditorStyle::StyleInstance = nullptr;

void FDMXEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FDMXEditorStyle::Shutdown()
{
	ensureMsgf(StyleInstance.IsValid(), TEXT("%S called, but StyleInstance wasn't initialized"), __FUNCTION__);
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FDMXEditorStyle::GetStyleSetName()
{
	return TEXT("DMXEditorStyle");
}

TSharedRef<FSlateStyleSet> FDMXEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("DMXEngine")->GetBaseDir() / TEXT("Resources"));

	// Solid color brushes
	Style->Set("DMXEditor.WhiteBrush", new FSlateColorBrush(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)));
	Style->Set("DMXEditor.BlackBrush", new FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)));

	// Fonts
	const TCHAR* FontPathRoboto = TEXT("Font'/Engine/EngineFonts/Roboto.Roboto'");
	const UFont* FontRoboto = Cast<UFont>(StaticLoadObject(UFont::StaticClass(), nullptr, FontPathRoboto));
	check(FontRoboto != nullptr);

	Style->Set("DMXEditor.Font.InputChannelID", FSlateFontInfo(FontRoboto, 8, FName(TEXT("Light"))));
	Style->Set("DMXEditor.Font.InputChannelValue", FSlateFontInfo(FontRoboto, 10, FName(TEXT("Regular"))));

	return Style;
}

void FDMXEditorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FDMXEditorStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}