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

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

TSharedRef<FSlateStyleSet> FDMXEditorStyle::Create()
{
	static const FVector2D Icon40x40(40.0f, 40.0f);
	static const FVector2D Icon34x29(34.0f, 29.0f);
	static const FVector2D Icon51x31(51.0f, 31.0f);

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

	Style->Set("DMXEditor.Font.InputUniverseHeader", FSlateFontInfo(FontRoboto, 10, FName(TEXT("Bold"))));
	Style->Set("DMXEditor.Font.InputUniverseID", FSlateFontInfo(FontRoboto, 10, FName(TEXT("Regular"))));
	Style->Set("DMXEditor.Font.InputUniverseChannelID", FSlateFontInfo(FontRoboto, 10, FName(TEXT("Regular"))));
	Style->Set("DMXEditor.Font.InputUniverseChannelValue", FSlateFontInfo(FontRoboto, 10, FName(TEXT("Light"))));
	
	Style->Set("DMXEditor.InputInfoAction", new IMAGE_BRUSH(TEXT("ButtonIcon_40x"), Icon40x40));

	// Distribution Grid buttons
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.0.0", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_0.0", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.0.1", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_0.1", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.0.2", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_0.2", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.0.3", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_0.3", Icon34x29));

	Style->Set("DMXEditor.PixelMapping.DistributionGrid.1.0", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_1.0", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.1.1", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_1.1", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.1.2", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_1.2", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.1.3", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_1.3", Icon34x29));

	Style->Set("DMXEditor.PixelMapping.DistributionGrid.2.0", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_2.0", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.2.1", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_2.1", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.2.2", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_2.2", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.2.3", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_2.3", Icon34x29));

	Style->Set("DMXEditor.PixelMapping.DistributionGrid.3.0", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_3.0", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.3.1", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_3.1", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.3.2", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_3.2", Icon34x29));
	Style->Set("DMXEditor.PixelMapping.DistributionGrid.3.3", new IMAGE_BRUSH("Icons/DistributionGrid/PixelDirectionIcon_3.3", Icon34x29));

	Style->Set("DMXEditor.OutputConsole.MacroSineWave", new IMAGE_BRUSH("Icons/MacroSineWaveIcon51x31", Icon51x31));
	Style->Set("DMXEditor.OutputConsole.MacroMin", new IMAGE_BRUSH("Icons/MacroMinIcon51x31", Icon51x31));
	Style->Set("DMXEditor.OutputConsole.MacroMax", new IMAGE_BRUSH("Icons/MacroMaxIcon51x31", Icon51x31));
	return Style;
}
#undef IMAGE_BRUSH

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

