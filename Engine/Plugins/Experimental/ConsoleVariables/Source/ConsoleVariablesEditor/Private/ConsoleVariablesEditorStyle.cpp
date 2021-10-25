// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorStyle.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"

TSharedPtr<FSlateStyleSet> FConsoleVariablesEditorStyle::StyleInstance = nullptr;

void FConsoleVariablesEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FConsoleVariablesEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}


const FLinearColor& FConsoleVariablesEditorStyle::GetColor(const FName PropertyName, const ANSICHAR* Specifier, const FLinearColor& DefaultValue, const ISlateStyle* RequestingStyle) const
{
	return StyleInstance->GetColor(PropertyName, Specifier);
}

const FSlateBrush* FConsoleVariablesEditorStyle::GetBrush(const FName PropertyName, const ANSICHAR* Specifier, const ISlateStyle* RequestingStyle) const
{
	return StyleInstance->GetBrush(PropertyName, Specifier);
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )

const FVector2D Icon8x8(8.f, 8.f);
const FVector2D Icon64x64(64.f, 64.f);
const FVector2D Icon40x40(40.0f, 40.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon12x12(12.0f, 12.0f);

TSharedRef< FSlateStyleSet > FConsoleVariablesEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>("ConsoleVariablesEditor");

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ConsoleVariables"));
	check(Plugin.IsValid());
	if (Plugin.IsValid())
	{
		Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	}

	Style->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FButtonStyle Button = FButtonStyle()
		.SetNormal(FSlateBoxBrush(Style->RootToContentDir("ButtonHoverHint.png"), FMargin(4/16.0f), FLinearColor(1,1,1,0.15f)))
		.SetHovered(FSlateBoxBrush(Style->RootToContentDir("ButtonHoverHint.png"), FMargin(4/16.0f), FLinearColor(1,1,1,0.25f)))
		.SetPressed(FSlateBoxBrush(Style->RootToContentDir("ButtonHoverHint.png"), FMargin(4/16.0f), FLinearColor(1,1,1,0.30f)))
		.SetNormalPadding( FMargin(0,0,0,1) )
		.SetPressedPadding( FMargin(0,1,0,0) );

	FComboButtonStyle ComboButton = FComboButtonStyle()
		.SetButtonStyle(Button.SetNormal(FSlateNoResource()))
		.SetDownArrowImage(FSlateImageBrush(Style->RootToCoreContentDir(TEXT("Common/ComboArrow.png")), Icon8x8))
		.SetMenuBorderBrush(FSlateBoxBrush(Style->RootToCoreContentDir(TEXT("Old/Menu_Background.png")), FMargin(8.0f/64.0f)))
		.SetMenuBorderPadding(FMargin(0.0f));
	
	Style->Set("ComboButton", ComboButton);

	// Toolbar
	Style->Set("ConsoleVariables.ToolbarButton", new IMAGE_BRUSH("Icons/Icon40", Icon40x40));
	Style->Set("ConsoleVariables.ToolbarButton.Small", new IMAGE_BRUSH("Icons/Icon20", Icon20x20));

	// Brush
	Style->Set("ConsoleVariablesEditor.GroupBorder", new BOX_BRUSH("Common/DarkGroupBorder", FMargin(4.0f / 16.0f)));
	Style->Set("ConsoleVariablesEditor.BrightBorder", new FSlateColorBrush(FColor(112, 112, 112, 100)));

	// Border colors for Results view
	Style->Set("ConsoleVariablesEditor.HeaderRowBorder", new FSlateColorBrush(FColor::Black));
	Style->Set("ConsoleVariablesEditor.CommandGroupBorder", new BOX_BRUSH("Common/DarkGroupBorder", FMargin(4.0f / 16.0f)));
	Style->Set("ConsoleVariablesEditor.DefaultBorder", new FSlateColorBrush(FColor(0, 0, 0, 0)));

	// Buttons

	FTextBlockStyle ButtonTextStyle = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("ContentBrowser.TopBar.Font");
	FLinearColor ButtonTextColor = ButtonTextStyle.ColorAndOpacity.GetSpecifiedColor();
	ButtonTextColor.A /= 2;
	ButtonTextStyle.ColorAndOpacity = ButtonTextColor;
	ButtonTextStyle.ShadowColorAndOpacity.A /= 2;
	Style->Set("ConsoleVariablesEditor.Button.TextStyle", ButtonTextStyle);

	FTextBlockStyle AndTextStyle = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Graph.CompactNode.Title");
	FTextBlockStyle OrTextStyle = AndTextStyle;
	AndTextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("BoldCondensed", 16 ));
	OrTextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("BoldCondensed", 18 ));
	
	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH

void FConsoleVariablesEditorStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

const ISlateStyle& FConsoleVariablesEditorStyle::Get()
{
	return *StyleInstance;
}

const FName& FConsoleVariablesEditorStyle::GetStyleSetName() const
{
	static FName ConsoleVariablesStyleSetName(TEXT("ConsoleVariablesEditor"));
	return ConsoleVariablesStyleSetName;
}
