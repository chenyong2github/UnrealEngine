// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorWidgetsStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Classes/EditorStyleSettings.h"

TSharedPtr< FSlateStyleSet > FNiagaraEditorWidgetsStyle::NiagaraEditorWidgetsStyleInstance = NULL;
 
void FNiagaraEditorWidgetsStyle::Initialize()
{
	if (!NiagaraEditorWidgetsStyleInstance.IsValid())
	{
		NiagaraEditorWidgetsStyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*NiagaraEditorWidgetsStyleInstance);
	}
}

void FNiagaraEditorWidgetsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*NiagaraEditorWidgetsStyleInstance);
	ensure(NiagaraEditorWidgetsStyleInstance.IsUnique());
	NiagaraEditorWidgetsStyleInstance.Reset();
}

FName FNiagaraEditorWidgetsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("NiagaraEditorWidgetsStyle"));
	return StyleSetName;
}

NIAGARAEDITOR_API FString RelativePathToPluginPath(const FString& RelativePath, const ANSICHAR* Extension);

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( RelativePathToPluginPath( RelativePath, ".png" ), __VA_ARGS__ )
#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_PLUGIN_BRUSH( RelativePath, ... ) FSlateBoxBrush( RelativePathToPluginPath( RelativePath, ".png"), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)
#define IMAGE_CORE_BRUSH( RelativePath, ... ) FSlateImageBrush( FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png") , __VA_ARGS__ )
#define BOX_CORE_BRUSH( RelativePath, ... ) FSlateBoxBrush( FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png"), __VA_ARGS__ )

const FVector2D Icon6x6(6.0f, 6.0f);
const FVector2D Icon8x8(8.0f, 8.0f);
const FVector2D Icon8x16(8.0f, 16.0f);
const FVector2D Icon12x12(12.0f, 12.0f);
const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon30x30(30.0f, 30.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef< FSlateStyleSet > FNiagaraEditorWidgetsStyle::Create()
{
	const FTextBlockStyle NormalText = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("NormalText");
	const FEditableTextBoxStyle NormalEditableTextBox = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	const FSpinBoxStyle NormalSpinBox = FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("SpinBox");

	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("NiagaraEditorWidgetsStyle"));
	Style->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate/Niagara"));

	// Stack
	Style->Set("NiagaraEditor.Stack.IconSize", FVector2D(18.0f, 18.0f));

	FSlateFontInfo StackGroupFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle StackGroupText = FTextBlockStyle(NormalText)
		.SetFont(StackGroupFont)
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f))
		.SetShadowOffset(FVector2D(0, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));
	Style->Set("NiagaraEditor.Stack.GroupText", StackGroupText);

	FSlateFontInfo StackDefaultFont = DEFAULT_FONT("Regular", 10);
	FTextBlockStyle StackDefaultText = FTextBlockStyle(NormalText)
		.SetFont(StackDefaultFont);
	Style->Set("NiagaraEditor.Stack.DefaultText", StackDefaultText);

	FSlateFontInfo StackCategoryFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle StackCategoryText = FTextBlockStyle(NormalText)
		.SetFont(StackCategoryFont)
		.SetShadowOffset(FVector2D(1, 1));
	Style->Set("NiagaraEditor.Stack.CategoryText", StackCategoryText);

	FSlateFontInfo ParameterFont = DEFAULT_FONT("Regular", 8);
	FTextBlockStyle ParameterText = FTextBlockStyle(NormalText)
		.SetFont(ParameterFont);
	Style->Set("NiagaraEditor.Stack.ParameterText", ParameterText);

	FSlateFontInfo ParameterCollectionFont = DEFAULT_FONT("Regular", 9);
	FTextBlockStyle ParameterCollectionText = FTextBlockStyle(NormalText)
		.SetFont(ParameterCollectionFont);
	Style->Set("NiagaraEditor.Stack.ParameterCollectionText", ParameterCollectionText);

	FSlateFontInfo StackItemFont = DEFAULT_FONT("Regular", 11);
	FTextBlockStyle StackItemText = FTextBlockStyle(NormalText)
		.SetFont(StackItemFont);
	Style->Set("NiagaraEditor.Stack.ItemText", StackItemText);

	FSlateFontInfo SystemOverviewListHeaderFont = DEFAULT_FONT("Bold", 12);
	FTextBlockStyle SystemOverviewListHeaderText = FTextBlockStyle(NormalText)
		.SetFont(SystemOverviewListHeaderFont);
	Style->Set("NiagaraEditor.SystemOverview.ListHeaderText", SystemOverviewListHeaderText);

	FSlateFontInfo SystemOverviewGroupHeaderFont = DEFAULT_FONT("Bold", 9);
	FTextBlockStyle SystemOverviewGroupHeaderText = FTextBlockStyle(NormalText)
		.SetFont(SystemOverviewGroupHeaderFont)
		.SetShadowOffset(FVector2D(0, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));;
	Style->Set("NiagaraEditor.SystemOverview.GroupHeaderText", SystemOverviewGroupHeaderText);

	FSlateFontInfo SystemOverviewItemFont = DEFAULT_FONT("Regular", 9);
	FTextBlockStyle SystemOverviewItemText = FTextBlockStyle(NormalText)
		.SetFont(SystemOverviewItemFont);
	Style->Set("NiagaraEditor.SystemOverview.ItemText", SystemOverviewItemText);

	Style->Set("NiagaraEditor.SystemOverview.Item.BackgroundColor", FLinearColor(FColor(62, 62, 62)));
	Style->Set("NiagaraEditor.SystemOverview.Group.BackgroundColor", FLinearColor::Transparent);
	Style->Set("NiagaraEditor.SystemOverview.CheckBoxColor", FLinearColor(FColor(160, 160, 160)));
	Style->Set("NiagaraEditor.SystemOverview.CheckBoxBorder", new BOX_CORE_BRUSH("Common/GroupBorderLight", FMargin(4.0f / 16.0f)));
	Style->Set("NiagaraEditor.SystemOverview.NodeBackgroundBorder", new BOX_PLUGIN_BRUSH("Icons/SystemOverviewNodeBackground", FMargin(1.0f / 4.0f)));
	Style->Set("NiagaraEditor.SystemOverview.NodeBackgroundColor", FLinearColor(FColor(48, 48, 48)));

	const FTableRowStyle& NormalTableRowStyle = FEditorStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");

	FSlateBrush StackRowSelectionBrush = BOX_PLUGIN_BRUSH("Icons/StackSelectionBorder", FMargin(2.0f / 8.0f), GetDefault<UEditorStyleSettings>()->SelectionColor);
	FSlateBrush StackRowSubduedSelectionBrush = BOX_PLUGIN_BRUSH("Icons/StackSelectionBorder", FMargin(2.0f / 8.0f), GetDefault<UEditorStyleSettings>()->GetSubduedSelectionColor());
	Style->Set("NiagaraEditor.Stack.TableViewRow", FTableRowStyle(NormalTableRowStyle)
		.SetActiveBrush(StackRowSelectionBrush)
		.SetActiveHoveredBrush(StackRowSelectionBrush)
		.SetInactiveBrush(StackRowSubduedSelectionBrush)
		.SetInactiveHoveredBrush(StackRowSelectionBrush));

	Style->Set("NiagaraEditor.SystemOverview.TableViewRow", FTableRowStyle(NormalTableRowStyle)
		.SetInactiveBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, GetDefault<UEditorStyleSettings>()->GetSubduedSelectionColor())));

	Style->Set("NiagaraEditor.Stack.BackgroundColor", FLinearColor(FColor(96, 96, 96)));
	Style->Set("NiagaraEditor.Stack.Item.HeaderBackgroundColor", FLinearColor(FColor(48, 48, 48)));
	Style->Set("NiagaraEditor.Stack.Item.ContentBackgroundColor", FLinearColor(FColor(62, 62, 62)));
	Style->Set("NiagaraEditor.Stack.Item.ContentAdvancedBackgroundColor", FLinearColor(FColor(53, 53, 53)));
	Style->Set("NiagaraEditor.Stack.Item.FooterBackgroundColor", FLinearColor(FColor(75, 75, 75)));
	Style->Set("NiagaraEditor.Stack.Item.IssueBackgroundColor", FLinearColor(FColor(120, 120, 62)));
	Style->Set("NiagaraEditor.Stack.UnknownColor", FLinearColor(1, 0, 1));

	Style->Set("NiagaraEditor.Stack.ItemHeaderFooter.BackgroundBrush", new FSlateColorBrush(FLinearColor(FColor(20, 20, 20))));

	Style->Set("NiagaraEditor.Stack.ForegroundColor", FLinearColor(FColor(220, 220, 220)));
	Style->Set("NiagaraEditor.Stack.GroupForegroundColor", FLinearColor(FColor(220, 220, 220)));
	Style->Set("NiagaraEditor.Stack.FlatButtonColor", FLinearColor(FColor(205, 205, 205)));

	Style->Set("NiagaraEditor.Stack.HighlightedButtonBrush", new BOX_CORE_BRUSH("Common/ButtonHoverHint", FMargin(4 / 16.0f), GetDefault<UEditorStyleSettings>()->SelectionColor));

	const FVector2D ViewOptionsShadowOffset = FVector2D(0, 1);
	Style->Set("NiagaraEditor.Stack.ViewOptionsShadowOffset", ViewOptionsShadowOffset);

	FComboButtonStyle ViewOptionsComboButtonStyle = FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");

	const FLinearColor ViewOptionsShadowColor = FLinearColor::Black;
	Style->Set("NiagaraEditor.Stack.ViewOptionsShadowColor", FLinearColor::Black);
	Style->Set("NiagaraEditor.Stack.ViewOptionsButton",	ViewOptionsComboButtonStyle
		.SetButtonStyle(FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("HoverHintOnly"))
		.SetShadowOffset(ViewOptionsShadowOffset)
		.SetShadowColorAndOpacity(ViewOptionsShadowColor)
	);

	Style->Set("NiagaraEditor.Stack.AccentColor.System", FLinearColor(FColor(67, 105, 124)));
	Style->Set("NiagaraEditor.Stack.AccentColor.Emitter", FLinearColor(FColor(126, 87, 67)));
	Style->Set("NiagaraEditor.Stack.AccentColor.Particle", FLinearColor(FColor(87, 107, 61)));
	Style->Set("NiagaraEditor.Stack.AccentColor.Render", FLinearColor(FColor(134, 80, 80)));
	Style->Set("NiagaraEditor.Stack.AccentColor.None", FLinearColor::Transparent);

	Style->Set("NiagaraEditor.Stack.IconColor.System", FLinearColor(FColor(1, 202, 252)));
	Style->Set("NiagaraEditor.Stack.IconColor.Emitter", FLinearColor(FColor(241, 99, 6)));
	Style->Set("NiagaraEditor.Stack.IconColor.Particle", FLinearColor(FColor(131, 218, 9)));
	Style->Set("NiagaraEditor.Stack.IconColor.Render", FLinearColor(FColor(230, 102, 102)));

 	Style->Set("NiagaraEditor.Stack.DropTarget.BackgroundColor", FLinearColor(1.0f, 1.0f, 1.0f, 0.25f));
 	Style->Set("NiagaraEditor.Stack.DropTarget.BackgroundColorHover", FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
	Style->Set("NiagaraEditor.Stack.DropTarget.BorderVertical", new IMAGE_PLUGIN_BRUSH("Icons/StackDropTargetBorder_Vertical", FVector2D(2, 8), FLinearColor::White, ESlateBrushTileType::Vertical));
	Style->Set("NiagaraEditor.Stack.DropTarget.BorderHorizontal", new IMAGE_PLUGIN_BRUSH("Icons/StackDropTargetBorder_Horizontal", FVector2D(8, 2), FLinearColor::White, ESlateBrushTileType::Horizontal));

	Style->Set("NiagaraEditor.Stack.GoToSourceIcon", new IMAGE_CORE_BRUSH("Common/GoToSource", Icon30x30, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.ParametersIcon", new IMAGE_PLUGIN_BRUSH("Icons/SystemParams", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.SpawnIcon", new IMAGE_PLUGIN_BRUSH("Icons/Spawn", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.UpdateIcon", new IMAGE_PLUGIN_BRUSH("Icons/Update", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.EventIcon", new IMAGE_PLUGIN_BRUSH("Icons/Event", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.ShaderStageIcon", new IMAGE_PLUGIN_BRUSH("Icons/ShaderStage", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.RenderIcon", new IMAGE_PLUGIN_BRUSH("Icons/Render", Icon12x12, FLinearColor::White));

	Style->Set("NiagaraEditor.Stack.ParametersIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/SystemParams", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.SpawnIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Spawn", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.UpdateIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Update", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.EventIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Event", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.ShaderStageIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/ShaderStage", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.RenderIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Render", Icon16x16, FLinearColor::White));

	Style->Set("NiagaraEditor.Stack.IconHighlightedSize", 16.0f);

	Style->Set("NiagaraEditor.Stack.Splitter", FSplitterStyle()
		.SetHandleNormalBrush(IMAGE_CORE_BRUSH("Common/SplitterHandleHighlight", Icon8x8, FLinearColor(.1f, .1f, .1f, 1.0f)))
		.SetHandleHighlightBrush(IMAGE_CORE_BRUSH("Common/SplitterHandleHighlight", Icon8x8, FLinearColor::White))
	);

	Style->Set("NiagaraEditor.Stack.SearchHighlightColor", FEditorStyle::GetColor("TextBlock.HighlighColor"));
	Style->Set("NiagaraEditor.Stack.SearchResult", new BOX_PLUGIN_BRUSH("Icons/SearchResultBorder", FMargin(1.f/8.f)));

	Style->Set("NiagaraEditor.Stack.AddButton", FButtonStyle()
		.SetNormal(BOX_CORE_BRUSH("Common/FlatButton", 2.0f / 8.0f, FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)))
		.SetHovered(BOX_CORE_BRUSH("Common/FlatButton", 2.0f / 8.0f, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)))
		.SetPressed(BOX_CORE_BRUSH("Common/FlatButton", 2.0f / 8.0f, FLinearColor(.8f, .8f, .8f, 1.0f)))
	);

	FTextBlockStyle AddButtonText = FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f))
		.SetShadowOffset(FVector2D(0, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));
	Style->Set("NiagaraEditor.Stack.AddButtonText", AddButtonText);

	Style->Set("NiagaraEditor.Stack.ModuleHighlight", new IMAGE_PLUGIN_BRUSH("Icons/ModuleHighlight", Icon6x6, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.ModuleHighlightMore", new IMAGE_PLUGIN_BRUSH("Icons/ModuleHighlightMore", Icon6x6, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.ModuleHighlightLarge", new IMAGE_PLUGIN_BRUSH("Icons/ModuleHighlightLarge", Icon8x8, FLinearColor::White));

	Style->Set("NiagaraEditor.ShowInCurveEditorIcon", new IMAGE_PLUGIN_BRUSH("Icons/ShowInCurveEditor", Icon16x16, FLinearColor::White));

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef DEFAULT_FONT
#undef BOX_CORE_BRUSH
#undef IMAGE_CORE_BRUSH

void FNiagaraEditorWidgetsStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

const ISlateStyle& FNiagaraEditorWidgetsStyle::Get()
{
	return *NiagaraEditorWidgetsStyleInstance;
}
