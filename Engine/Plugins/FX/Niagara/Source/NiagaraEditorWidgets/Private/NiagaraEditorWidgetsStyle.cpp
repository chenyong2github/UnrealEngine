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
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

	Style->Set("NiagaraEditor.Stack.GroupText", StackGroupText);
	
	FEditableTextBoxStyle StackEditableGroupText = FEditableTextBoxStyle(NormalEditableTextBox)
		.SetFont(StackGroupFont);

	FInlineEditableTextBlockStyle StackInlineEditableGroupText = FInlineEditableTextBlockStyle()
		.SetEditableTextBoxStyle(StackEditableGroupText)
		.SetTextStyle(StackGroupText); 

	Style->Set("NiagaraEditor.Stack.EditableGroupText", StackInlineEditableGroupText);

	FSlateFontInfo StackDefaultFont = DEFAULT_FONT("Regular", 10);
	FTextBlockStyle StackDefaultText = FTextBlockStyle(NormalText)
		.SetFont(StackDefaultFont);
	Style->Set("NiagaraEditor.Stack.DefaultText", StackDefaultText);

	FSlateFontInfo StackCategoryFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle StackCategoryText = FTextBlockStyle(NormalText)
		.SetFont(StackCategoryFont);
	Style->Set("NiagaraEditor.Stack.CategoryText", StackCategoryText);
	Style->Set("NiagaraEditor.SystemOverview.GroupHeaderText", StackCategoryText);

	FSlateFontInfo ParameterFont = DEFAULT_FONT("Regular", 8);
	FTextBlockStyle ParameterText = FTextBlockStyle(NormalText)
		.SetFont(ParameterFont);
	Style->Set("NiagaraEditor.Stack.ParameterText", ParameterText);

	FSlateFontInfo TextContentFont = DEFAULT_FONT("Regular", 9);
	FTextBlockStyle TextContentText = FTextBlockStyle(NormalText)
		.SetFont(TextContentFont);
	Style->Set("NiagaraEditor.Stack.TextContentText", TextContentText);

	FSlateFontInfo StackItemFont = DEFAULT_FONT("Regular", 11);
	FTextBlockStyle StackItemText = FTextBlockStyle(NormalText)
		.SetFont(StackItemFont);
	Style->Set("NiagaraEditor.Stack.ItemText", StackItemText);

	FSlateFontInfo PerfWidgetDetailFont = DEFAULT_FONT("Regular", 7);
	Style->Set("NiagaraEditor.Stack.Stats.DetailFont", PerfWidgetDetailFont);
	FSlateFontInfo PerfWidgetGroupFont = DEFAULT_FONT("Regular", 8);
	Style->Set("NiagaraEditor.Stack.Stats.GroupFont", PerfWidgetGroupFont);
	FSlateFontInfo PerfWidgetEvalTypeFont = DEFAULT_FONT("Regular", 7);
	Style->Set("NiagaraEditor.Stack.Stats.EvalTypeFont", PerfWidgetEvalTypeFont);
	
	FEditableTextBoxStyle StackEditableItemText = FEditableTextBoxStyle(NormalEditableTextBox)
		.SetFont(StackItemFont);

	FInlineEditableTextBlockStyle StackInlineEditableItemText = FInlineEditableTextBlockStyle()
		.SetTextStyle(StackItemText)
		.SetEditableTextBoxStyle(StackEditableItemText);
	Style->Set("NiagaraEditor.Stack.EditableItemText", StackInlineEditableItemText);

	FSlateFontInfo StackSubduedItemFont = DEFAULT_FONT("Regular", 9);
	FTextBlockStyle StackSubduedItemText = FTextBlockStyle(NormalText)
		.SetFont(StackSubduedItemFont);
	Style->Set("NiagaraEditor.Stack.SubduedItemText", StackSubduedItemText);

	FSlateFontInfo SystemOverviewListHeaderFont = DEFAULT_FONT("Bold", 12);
	FTextBlockStyle SystemOverviewListHeaderText = FTextBlockStyle(NormalText)
		.SetFont(SystemOverviewListHeaderFont);
	Style->Set("NiagaraEditor.SystemOverview.ListHeaderText", SystemOverviewListHeaderText);

	FSlateFontInfo SystemOverviewItemFont = DEFAULT_FONT("Regular", 9);
	FTextBlockStyle SystemOverviewItemText = FTextBlockStyle(NormalText)
		.SetFont(SystemOverviewItemFont);
	Style->Set("NiagaraEditor.SystemOverview.ItemText", SystemOverviewItemText);

	FSlateFontInfo SystemOverviewAlternateItemFont = DEFAULT_FONT("Italic", 9);
	FTextBlockStyle SystemOverviewAlternateItemText = FTextBlockStyle(NormalText)
		.SetFont(SystemOverviewAlternateItemFont);
	Style->Set("NiagaraEditor.SystemOverview.AlternateItemText", SystemOverviewAlternateItemText);

	Style->Set("NiagaraEditor.SystemOverview.Item.BackgroundColor", FLinearColor(FColor(62, 62, 62)));
	Style->Set("NiagaraEditor.SystemOverview.Group.BackgroundColor", FLinearColor::Transparent);
	Style->Set("NiagaraEditor.SystemOverview.CheckBoxColor", FLinearColor(FColor(160, 160, 160)));
	Style->Set("NiagaraEditor.SystemOverview.CheckBoxBorder", new BOX_CORE_BRUSH("Common/GroupBorderLight", FMargin(4.0f / 16.0f)));
	Style->Set("NiagaraEditor.SystemOverview.NodeBackgroundBorder", new BOX_PLUGIN_BRUSH("Icons/SystemOverviewNodeBackground", FMargin(1.0f / 4.0f)));
	Style->Set("NiagaraEditor.SystemOverview.NodeBackgroundColor", FLinearColor(FColor(48, 48, 48)));

	FSlateFontInfo CurveOverviewTopLevelFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle CurveOverviewTopLevelText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewTopLevelFont);
	Style->Set("NiagaraEditor.CurveOverview.TopLevelText", CurveOverviewTopLevelText);

	FSlateFontInfo CurveOverviewScriptFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle CurveOverviewScriptText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewScriptFont);
	Style->Set("NiagaraEditor.CurveOverview.ScriptText", CurveOverviewScriptText);

	FSlateFontInfo CurveOverviewModuleFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle CurveOverviewModuleText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewModuleFont);
	Style->Set("NiagaraEditor.CurveOverview.ModuleText", CurveOverviewModuleText);

	FSlateFontInfo CurveOverviewInputFont = DEFAULT_FONT("Bold", 9);
	FTextBlockStyle CurveOverviewInputText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewInputFont);
	Style->Set("NiagaraEditor.CurveOverview.InputText", CurveOverviewInputText);

	FSlateFontInfo CurveOverviewDataInterfaceFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle CurveOverviewDataInterfaceText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewDataInterfaceFont);
	Style->Set("NiagaraEditor.CurveOverview.DataInterfaceText", CurveOverviewDataInterfaceText);

	FSlateFontInfo CurveOverviewSecondaryFont = DEFAULT_FONT("Italic", 9);
	FTextBlockStyle CurveOverviewSecondaryText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewSecondaryFont);
	Style->Set("NiagaraEditor.CurveOverview.SecondaryText", CurveOverviewSecondaryText);

	FSlateFontInfo CurveOverviewCurveComponentFont = DEFAULT_FONT("Regular", 9);
	FTextBlockStyle CurveOverviewCurveComponentText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewCurveComponentFont);
	Style->Set("NiagaraEditor.CurveOverview.CurveComponentText", CurveOverviewCurveComponentText);

	FSlateFontInfo CurveOverviewDefaultFont = DEFAULT_FONT("Bold", 10);
	FTextBlockStyle CurveOverviewDefaultText = FTextBlockStyle(NormalText)
		.SetFont(CurveOverviewDefaultFont);
	Style->Set("NiagaraEditor.CurveOverview.DefaultText", CurveOverviewDefaultText);

	Style->Set("NiagaraEditor.CurveDetails.TextButtonForeground", FLinearColor::White);

	Style->Set("NiagaraEditor.CurveDetails.Import.Small", new IMAGE_CORE_BRUSH("Icons/GeneralTools/Import_40x", Icon20x20));

	Style->Set("NiagaraEditor.CurveDetails.ShowInOverview.Small", new IMAGE_CORE_BRUSH("Common/GoToSource", Icon12x12, FLinearColor(.9f, .9f, .9f, 1.0f)));

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
	Style->Set("NiagaraEditor.Stack.Item.CustomNoteBackgroundColor", FLinearColor(FColor(56, 111, 75)));
	Style->Set("NiagaraEditor.Stack.Item.InfoBackgroundColor", FLinearColor(FColor(68, 100, 106)));
	Style->Set("NiagaraEditor.Stack.Item.WarningBackgroundColor", FLinearColor(FColor(97, 97, 68)));
	Style->Set("NiagaraEditor.Stack.Item.ErrorBackgroundColor", FLinearColor(FColor(126, 78, 68)));

	Style->Set("NiagaraEditor.Stack.UnknownColor", FLinearColor(1, 0, 1));

	Style->Set("NiagaraEditor.Stack.ItemHeaderFooter.BackgroundBrush", new FSlateColorBrush(FLinearColor(FColor(20, 20, 20))));

	Style->Set("NiagaraEditor.Stack.ForegroundColor", FLinearColor(FColor(220, 220, 220)));
	Style->Set("NiagaraEditor.Stack.GroupForegroundColor", FLinearColor(FColor(220, 220, 220)));
	Style->Set("NiagaraEditor.Stack.FlatButtonColor", FLinearColor(FColor(205, 205, 205)));
	Style->Set("NiagaraEditor.Stack.DividerColor", FLinearColor(FColor(92, 92, 92)));
	
	Style->Set("NiagaraEditor.Stack.Stats.EvalTypeColor", FLinearColor(FColor(168, 168, 168)));
	Style->Set("NiagaraEditor.Stack.Stats.RuntimePlaceholderColor", FLinearColor(FColor(86, 86, 86)));
	Style->Set("NiagaraEditor.Stack.Stats.RuntimeUsageColorDefault", FLinearColor(FColor(200, 60, 60)));
	Style->Set("NiagaraEditor.Stack.Stats.RuntimeUsageColorParticleUpdate", FLinearColor(FColor(246, 3, 142)));
	Style->Set("NiagaraEditor.Stack.Stats.RuntimeUsageColorParticleSpawn", FLinearColor(FColor(255, 181, 0)));
	Style->Set("NiagaraEditor.Stack.Stats.RuntimeUsageColorSystem", FLinearColor(FColor(20, 161, 255)));
	Style->Set("NiagaraEditor.Stack.Stats.RuntimeUsageColorEmitter", FLinearColor(FColor(241, 99, 6)));
	Style->Set("NiagaraEditor.Stack.Stats.LowCostColor", FLinearColor(FColor(143, 185, 130)));
	Style->Set("NiagaraEditor.Stack.Stats.MediumCostColor", FLinearColor(FColor(220, 210, 86)));
	Style->Set("NiagaraEditor.Stack.Stats.HighCostColor", FLinearColor(FColor(205, 114, 69)));
	Style->Set("NiagaraEditor.Stack.Stats.MaxCostColor", FLinearColor(FColor(200, 60, 60)));

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
	Style->Set("NiagaraEditor.Stack.IconColor.VersionUpgrade", FLinearColor(FColor(255, 181, 0)));

	Style->Set("NiagaraEditor.Stack.InputValueIconColor.Linked", FLinearColor(FColor::Purple));
	Style->Set("NiagaraEditor.Stack.InputValueIconColor.Data", FLinearColor(FColor::Yellow));
	Style->Set("NiagaraEditor.Stack.InputValueIconColor.Dynamic", FLinearColor(FColor::Cyan));
	Style->Set("NiagaraEditor.Stack.InputValueIconColor.Expression", FLinearColor(FColor::Green));
	Style->Set("NiagaraEditor.Stack.InputValueIconColor.Default", FLinearColor(FColor::White));

 	Style->Set("NiagaraEditor.Stack.DropTarget.BackgroundColor", FLinearColor(1.0f, 1.0f, 1.0f, 0.25f));
 	Style->Set("NiagaraEditor.Stack.DropTarget.BackgroundColorHover", FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
	Style->Set("NiagaraEditor.Stack.DropTarget.BorderVertical", new IMAGE_PLUGIN_BRUSH("Icons/StackDropTargetBorder_Vertical", FVector2D(2, 8), FLinearColor::White, ESlateBrushTileType::Vertical));
	Style->Set("NiagaraEditor.Stack.DropTarget.BorderHorizontal", new IMAGE_PLUGIN_BRUSH("Icons/StackDropTargetBorder_Horizontal", FVector2D(8, 2), FLinearColor::White, ESlateBrushTileType::Horizontal));

	Style->Set("NiagaraEditor.Stack.GoToSourceIcon", new IMAGE_CORE_BRUSH("Common/GoToSource", Icon30x30, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.ParametersIcon", new IMAGE_PLUGIN_BRUSH("Icons/SystemParams", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.SpawnIcon", new IMAGE_PLUGIN_BRUSH("Icons/Spawn", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.UpdateIcon", new IMAGE_PLUGIN_BRUSH("Icons/Update", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.EventIcon", new IMAGE_PLUGIN_BRUSH("Icons/Event", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.SimulationStageIcon", new IMAGE_PLUGIN_BRUSH("Icons/SimulationStage", Icon12x12, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.RenderIcon", new IMAGE_PLUGIN_BRUSH("Icons/Render", Icon12x12, FLinearColor::White));

	Style->Set("NiagaraEditor.Stack.ParametersIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/SystemParams", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.SpawnIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Spawn", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.UpdateIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Update", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.EventIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/Event", Icon16x16, FLinearColor::White));
	Style->Set("NiagaraEditor.Stack.SimulationStageIconHighlighted", new IMAGE_PLUGIN_BRUSH("Icons/SimulationStage", Icon16x16, FLinearColor::White));
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

	// Scratch pad
	FSlateFontInfo ScratchPadEditorHeaderFont = DEFAULT_FONT("Bold", 11);
	FTextBlockStyle ScratchPadEditorHeaderText = FTextBlockStyle(NormalText)
		.SetFont(ScratchPadEditorHeaderFont);
	Style->Set("NiagaraEditor.ScratchPad.EditorHeaderText", ScratchPadEditorHeaderText);

	FSlateFontInfo ScratchPadSubSectionHeaderFont = DEFAULT_FONT("Bold", 9);
	FTextBlockStyle ScratchPadSubSectionHeaderText = FTextBlockStyle(NormalText)
		.SetFont(ScratchPadSubSectionHeaderFont);
	Style->Set("NiagaraEditor.ScratchPad.SubSectionHeaderText", ScratchPadSubSectionHeaderText);

	FSlateBrush ScratchPadCategoryBrush = BOX_PLUGIN_BRUSH("Icons/CategoryRow", FMargin(2.0f / 8.0f), FLinearColor(FColor(48, 48, 48)));
	FSlateBrush ScratchPadHoveredCategoryBrush = BOX_PLUGIN_BRUSH("Icons/CategoryRow", FMargin(2.0f / 8.0f), FLinearColor(FColor(38, 38, 38)));
	Style->Set("NiagaraEditor.ScratchPad.CategoryRow", FTableRowStyle(NormalTableRowStyle)
		.SetEvenRowBackgroundBrush(ScratchPadCategoryBrush)
		.SetOddRowBackgroundBrush(ScratchPadCategoryBrush)
		.SetEvenRowBackgroundHoveredBrush(ScratchPadHoveredCategoryBrush)
		.SetOddRowBackgroundHoveredBrush(ScratchPadHoveredCategoryBrush));

	Style->Set("NiagaraEditor.Scope.Engine", FLinearColor(FColor(230, 102, 102)));
	Style->Set("NiagaraEditor.Scope.Owner", FLinearColor(FColor(210, 112, 112)));
	Style->Set("NiagaraEditor.Scope.User", FLinearColor(FColor(114, 226, 254)));
	Style->Set("NiagaraEditor.Scope.System", FLinearColor(FColor(1, 202, 252)));
	Style->Set("NiagaraEditor.Scope.Emitter", FLinearColor(FColor(241, 99, 6)));
	Style->Set("NiagaraEditor.Scope.Particles", FLinearColor(FColor(131, 218, 9)));
	Style->Set("NiagaraEditor.Scope.ScriptPersistent", FLinearColor(FColor(255, 247, 77)));
	Style->Set("NiagaraEditor.Scope.ScriptTransient", FLinearColor(FColor(255, 247, 77)));

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
