// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardEditorStyle.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

const FName FSwitchboardEditorStyle::NAME_SwitchboardBrush = "Img.Switchboard.Small";


FSwitchboardEditorStyle::FSwitchboardEditorStyle()
	: FSlateStyleSet("SwitchboardEditorStyle")
{
	static const FVector2D Icon32x32(32.0f, 32.0f);

	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("Switchboard"))->GetContentDir();
	SetContentRoot(ContentDir);
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	Set(NAME_SwitchboardBrush, new FSlateImageBrush(RootToContentDir(TEXT("Icons/Switchboard_40x.png")), FVector2D(40.0f, 40.0f)));

	Set("Settings.RowBorder.Warning", new FSlateRoundedBoxBrush(FStyleColors::Transparent, 4.0f, FStyleColors::Warning, 1.0f));
	Set("Settings.RowBorder.Nominal", new FSlateRoundedBoxBrush(FStyleColors::Transparent, 4.0f, FStyleColors::Success, 1.0f));

	Set("Wizard.Background", new FSlateColorBrush(FStyleColors::Panel));

	{
		// These assets live in engine slate folders; change the root temporarily
		const FString PreviousContentRoot = GetContentRootDir();
		ON_SCOPE_EXIT{ SetContentRoot(PreviousContentRoot); };

		const FString EngineSlateDir = FPaths::EngineContentDir() / TEXT("Slate");
		SetContentRoot(EngineSlateDir);

		Set("Settings.Icons.Warning", new IMAGE_BRUSH_SVG("Starship/Common/alert-triangle-large", Icon32x32, FStyleColors::Warning));
		Set("Settings.Icons.Nominal", new IMAGE_BRUSH_SVG("Starship/Common/alert-triangle-large", Icon32x32, FStyleColors::Success));
	}

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FSwitchboardEditorStyle::~FSwitchboardEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FSwitchboardEditorStyle& FSwitchboardEditorStyle::Get()
{
	static FSwitchboardEditorStyle Inst;
	return Inst;
}
