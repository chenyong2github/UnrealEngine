// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RemoteControlPanelStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"
#include "Widgets/Input/SButton.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush(FRemoteControlPanelStyle::InContent(RelativePath, ".png" ), __VA_ARGS__)
#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define CORE_BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush(StyleSet->RootToCoreContentDir(RelativePath, TEXT(".png") ), __VA_ARGS__)
#define BOX_PLUGIN_BRUSH( RelativePath, ... ) FSlateBoxBrush(FRemoteControlPanelStyle::InContent( RelativePath, ".png" ), __VA_ARGS__)
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

TSharedPtr<FSlateStyleSet> FRemoteControlPanelStyle::StyleSet;

void FRemoteControlPanelStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}
	
	const FVector2D Icon8x8(8.0f, 8.0f);
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	StyleSet->Set("ClassThumbnail.RemoteControlPreset", new IMAGE_PLUGIN_BRUSH("Icons/RemoteControlAPI_64x", Icon64x64));

	FButtonStyle ExposeFunctionButtonStyle = FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("PropertyEditor.AssetComboStyle");
	ExposeFunctionButtonStyle.Normal = BOX_BRUSH("Common/GroupBorder", FMargin(4.0f / 16.0f));
	ExposeFunctionButtonStyle.Normal.TintColor = FLinearColor(0, 0, 0, 0.1);
	StyleSet->Set("RemoteControlPanel.ExposeFunctionButton", ExposeFunctionButtonStyle);

	FHyperlinkStyle ObjectSectionNameStyle = FCoreStyle::Get().GetWidgetStyle<FHyperlinkStyle>("Hyperlink");
	FButtonStyle SectionNameButtonStyle = ObjectSectionNameStyle.UnderlineStyle;
	SectionNameButtonStyle.Normal = ObjectSectionNameStyle.UnderlineStyle.Hovered;
	StyleSet->Set("RemoteControlPanel.SectionNameButton", SectionNameButtonStyle);

	FTextBlockStyle SectionNameTextStyle = ObjectSectionNameStyle.TextStyle;
	SectionNameTextStyle.Font = FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle");
	StyleSet->Set("RemoteControlPanel.SectionName", SectionNameTextStyle);

	FButtonStyle UnexposeButtonStyle = FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton");
	UnexposeButtonStyle.Normal = FSlateNoResource();
	UnexposeButtonStyle.NormalPadding = FMargin(0, 1.5f);
	UnexposeButtonStyle.PressedPadding = FMargin(0, 1.5f);
	StyleSet->Set("RemoteControlPanel.UnexposeButton", UnexposeButtonStyle);

	FTextBlockStyle ButtonTextStyle = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("ContentBrowser.TopBar.Font");
	FLinearColor ButtonTextColor = ButtonTextStyle.ColorAndOpacity.GetSpecifiedColor();
	ButtonTextColor.A /= 2;
	ButtonTextStyle.ColorAndOpacity = ButtonTextColor;
	ButtonTextStyle.ShadowColorAndOpacity.A /= 2;
	StyleSet->Set("RemoteControlPanel.Button.TextStyle", ButtonTextStyle);

	// Default to transparent
	StyleSet->Set("RemoteControlPanel.ExposedFieldBorder", new FSlateNoResource());

	FEditableTextBoxStyle SectionNameTextBoxStyle = FCoreStyle::Get().GetWidgetStyle< FEditableTextBoxStyle >("NormalEditableTextBox");
	SectionNameTextBoxStyle.BackgroundImageNormal = BOX_BRUSH("Common/GroupBorderLight", FMargin(4.0f / 16.0f));
	StyleSet->Set("RemoteControlPanel.SectionNameTextBox", SectionNameTextBoxStyle);

	StyleSet->Set("RemoteControlPanel.Settings", new IMAGE_BRUSH("Icons/GeneralTools/Settings_40x", Icon20x20));
	
	StyleSet->Set("RemoteControlPanel.GroupBorder", new BOX_BRUSH("Common/GroupBorder", FMargin(4.0f / 16.0f), FLinearColor(.5,.5,.5, 1.0f)));
	StyleSet->Set("RemoteControlPanel.HorizontalDash", new IMAGE_BRUSH("Common/HorizontalDottedLine_16x1px", FVector2D(16.0f, 1.0f), FLinearColor::White, ESlateBrushTileType::Horizontal));
	StyleSet->Set("RemoteControlPanel.VerticalDash", new IMAGE_BRUSH("Common/VerticalDottedLine_1x16px", FVector2D(1.0f, 16.0f), FLinearColor::White, ESlateBrushTileType::Vertical));

	FLinearColor NewSelectionColor = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row").ActiveBrush.TintColor.GetSpecifiedColor();
	NewSelectionColor.R *= 1.8;
	NewSelectionColor.G *= 1.8;
	NewSelectionColor.B *= 1.8;
	NewSelectionColor.A = 0.4;


	StyleSet->Set("RemoteControlPanel.GroupRowSelected", new BOX_BRUSH("Common/GroupBorderLight", FMargin(4.0f / 16.0f), NewSelectionColor));
	StyleSet->Set("RemoteControlPanel.TransparentBorder", new FSlateColorBrush(FColor::Transparent));

	FTableRowStyle GroupRowStyle = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	GroupRowStyle.SetSelectorFocusedBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupRowSelected"));
	GroupRowStyle.SetEvenRowBackgroundBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder"));
	GroupRowStyle.SetOddRowBackgroundBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder"));
	GroupRowStyle.SetEvenRowBackgroundHoveredBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder"));
	GroupRowStyle.SetOddRowBackgroundHoveredBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder"));
	GroupRowStyle.SetActiveHoveredBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupRowSelected"));
	GroupRowStyle.SetInactiveHoveredBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupRowSelected"));
	GroupRowStyle.SetActiveBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupRowSelected"));
	GroupRowStyle.SetInactiveBrush(*FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupRowSelected"));
	
	StyleSet->Set("RemoteControlPanel.GroupRow", GroupRowStyle);
	
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FRemoteControlPanelStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

TSharedPtr<ISlateStyle> FRemoteControlPanelStyle::Get()
{
	return StyleSet;
}

FName FRemoteControlPanelStyle::GetStyleSetName()
{
	static const FName RemoteControlPanelStyleName(TEXT("RemoteControlPanelStyle"));
	return RemoteControlPanelStyleName;
}

FString FRemoteControlPanelStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static const FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("RemoteControl"))->GetBaseDir() + TEXT("/Resources");
	return (ContentDir / RelativePath) + Extension;
}

#undef IMAGE_PLUGIN_BRUSH
#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BOX_PLUGIN_BRUSH
#undef DEFAULT_FONT
