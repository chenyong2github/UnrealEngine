// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithContentEditorStyle.h"

#include "DatasmithContentModule.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FDatasmithContentEditorStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )

TSharedPtr<FSlateStyleSet> FDatasmithContentEditorStyle::StyleSet;

void FDatasmithContentEditorStyle::Initialize()
{
	using namespace CoreStyleConstants;

	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	StyleSet->Set("DatasmithDataprepEditor.Importer", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithImporterIcon40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.Importer.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithImporterIcon40", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.Importer.Selected", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithImporterIcon40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.Importer.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithImporterIcon40", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.CADImporter", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithCADImporterIcon40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.CADImporter.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithCADImporterIcon40", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.CADImporter.Selected", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithCADImporterIcon40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.CADImporter.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithCADImporterIcon40", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.VREDImporter", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithVREDImporter40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.VREDImporter.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithVREDImporter40", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.VREDImporter.Selected", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithVREDImporter40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.VREDImporter.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithVREDImporter40", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.DeltaGenImporter", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithDeltaGenImporter40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.DeltaGenImporter.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithDeltaGenImporter40", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.DeltaGenImporter.Selected", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithDeltaGenImporter40", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.DeltaGenImporter.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/DatasmithDeltaGenImporter40", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.SaveScene", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.SaveScene.Small", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.SaveScene.Selected", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.SaveScene.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.ShowDatasmithSceneSettings", new IMAGE_PLUGIN_BRUSH("Icons/IconOptions", Icon40x40));

	StyleSet->Set("DatasmithDataprepEditor.BuildWorld", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.BuildWorld.Small", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.BuildWorld.Selected", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.BuildWorld.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.ExecutePipeline", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.ExecutePipeline.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.ExecutePipeline.Selected", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.ExecutePipeline.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon20x20));

	StyleSet->Set("DatasmithDataprepEditor.Jacketing", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.Jacketing.Small", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon20x20));
	StyleSet->Set("DatasmithDataprepEditor.Jacketing.Selected", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon40x40));
	StyleSet->Set("DatasmithDataprepEditor.Jacketing.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon20x20));

	// Copy the base style, and change the rounding of the left-side borders so that it can be combined with a "right" widget.
	FButtonStyle ButtonLeftStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
	ButtonLeftStyle.SetNormal(FSlateRoundedBoxBrush(FStyleColors::Secondary, FVector4(4.0f, 0.0f, 0.0f, 4.0f), FStyleColors::Input, InputFocusThickness))
		.SetHovered(FSlateRoundedBoxBrush(FStyleColors::Hover, FVector4(4.0f, 0.0f, 0.0f, 4.0f), FStyleColors::Input, InputFocusThickness))
		.SetPressed(FSlateRoundedBoxBrush(FStyleColors::Header, FVector4(4.0f, 0.0f, 0.0f, 4.0f), FStyleColors::Input, InputFocusThickness))
		.SetDisabled(FSlateRoundedBoxBrush(FStyleColors::Dropdown, FVector4(4.0f, 0.0f, 0.0f, 4.0f), FStyleColors::Recessed, InputFocusThickness));

	StyleSet->Set("DatasmithDataprepEditor.ButtonLeft", ButtonLeftStyle);

	// Copy the base style and tweak the padding and right-side border rounding so that it can be combined with a "left" widget.
	FComboBoxStyle ComboRightStyle = FAppStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("SimpleComboBox"));
	FComboButtonStyle& ComboButtonStyle = ComboRightStyle.ComboButtonStyle;
	ComboButtonStyle.SetDownArrowPadding(1);
	FButtonStyle& SimpleButtonStyle = ComboButtonStyle.ButtonStyle;
	SimpleButtonStyle
		.SetNormal(FSlateRoundedBoxBrush(FStyleColors::Secondary, FVector4(0.0f, 4.0f, 4.0f, 0.0f), FStyleColors::Input, InputFocusThickness))
		.SetHovered(FSlateRoundedBoxBrush(FStyleColors::Hover, FVector4(0.0f, 4.0f, 4.0f, 0.0f), FStyleColors::Input, InputFocusThickness))
		.SetPressed(FSlateRoundedBoxBrush(FStyleColors::Header, FVector4(0.0f, 4.0f, 4.0f, 0.0f), FStyleColors::Input, InputFocusThickness))
		.SetDisabled(FSlateRoundedBoxBrush(FStyleColors::Dropdown, FVector4(0.0f, 4.0f, 4.0f, 0.0f), FStyleColors::Recessed, InputFocusThickness));

	StyleSet->Set("DatasmithDataprepEditor.SimpleComboBoxRight", ComboRightStyle);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FDatasmithContentEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

FName FDatasmithContentEditorStyle::GetStyleSetName()
{
	static FName StyleName("DatasmithContentEditorStyle");
	return StyleName;
}

FString FDatasmithContentEditorStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString BaseDir = IPluginManager::Get().FindPlugin(DATASMITHCONTENT_MODULE_NAME)->GetBaseDir() + TEXT("/Resources");
	return (BaseDir / RelativePath) + Extension;
}
