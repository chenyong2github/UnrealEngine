// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkit/RenderPageCollectionEditorToolbar.h"
#include "BlueprintModes/RenderPagesApplicationModes.h"
#include "Commands/RenderPagesEditorCommands.h"
#include "IRenderPageCollectionEditor.h"
#include "Styles/RenderPagesEditorStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDocumentation.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SToolTip.h"
#include "WorkflowOrientedApp/SModeWidget.h"

#define LOCTEXT_NAMESPACE "RenderPages"


//////////////////////////////////////////////////////////////////////////
// SBlueprintModeSeparator

class SBlueprintModeSeparator : public SBorder
{
public:
	SLATE_BEGIN_ARGS(SBlueprintModeSeparator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArg)
	{
		SBorder::Construct(
			SBorder::FArguments()
			.BorderImage(FAppStyle::GetBrush("BlueprintEditor.PipelineSeparator"))
			.Padding(0.0f)
		);
	}

	// SWidget interface
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		constexpr float Height = 20.0f;
		constexpr float Thickness = 16.0f;
		return FVector2D(Thickness, Height);
	}
	// End of SWidget interface
};

//////////////////////////////////////////////////////////////////////////
// FRenderPagesBlueprintEditorToolbar

UE::RenderPages::Private::FRenderPagesBlueprintEditorToolbar::FRenderPagesBlueprintEditorToolbar(TSharedPtr<IRenderPageCollectionEditor>& InRenderPagesEditor)
	: BlueprintEditorWeakPtr(InRenderPagesEditor)
{}

void UE::RenderPages::Private::FRenderPagesBlueprintEditorToolbar::AddRenderPagesBlueprintEditorModesToolbar(TSharedPtr<FExtender> Extender)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		Extender->AddToolBarExtension(
			"Asset",
			EExtensionHook::After,
			BlueprintEditor->GetToolkitCommands(),
			FToolBarExtensionDelegate::CreateSP(this, &FRenderPagesBlueprintEditorToolbar::FillRenderPagesBlueprintEditorModesToolbar));
	}
}

void UE::RenderPages::Private::FRenderPagesBlueprintEditorToolbar::AddListingModeToolbar(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("Tools");

	Section.InsertPosition = FToolMenuInsert("Asset", EToolMenuInsertType::After);

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		FRenderPagesEditorCommands::Get().AddPage,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
	));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		FRenderPagesEditorCommands::Get().CopyPage,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Duplicate")
	));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		FRenderPagesEditorCommands::Get().DeletePage,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Minus")
	));
}

void UE::RenderPages::Private::FRenderPagesBlueprintEditorToolbar::AddLogicModeToolbar(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("Tools");

	Section.InsertPosition = FToolMenuInsert("Asset", EToolMenuInsertType::After);
}

void UE::RenderPages::Private::FRenderPagesBlueprintEditorToolbar::FillRenderPagesBlueprintEditorModesToolbar(FToolBarBuilder& ToolbarBuilder)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		UBlueprint* BlueprintObj = BlueprintEditor->GetBlueprintObj();
		if (!BlueprintObj || (!FBlueprintEditorUtils::IsLevelScriptBlueprint(BlueprintObj) && !FBlueprintEditorUtils::IsInterfaceBlueprint(BlueprintObj) && !BlueprintObj->bIsNewlyCreated))
		{
			TAttribute<FName> GetActiveMode(BlueprintEditor.ToSharedRef(), &FBlueprintEditor::GetCurrentMode);
			FOnModeChangeRequested SetActiveMode = FOnModeChangeRequested::CreateSP(BlueprintEditor.ToSharedRef(), &FBlueprintEditor::SetCurrentMode);

			// Left side padding
			BlueprintEditor->AddToolbarWidget(SNew(SSpacer).Size(FVector2D(4.0f, 1.0f)));

			BlueprintEditor->AddToolbarWidget(
				SNew(SModeWidget, FRenderPagesApplicationModes::GetLocalizedMode(FRenderPagesApplicationModes::ListingMode), FRenderPagesApplicationModes::ListingMode)
				.OnGetActiveMode(GetActiveMode)
				.OnSetActiveMode(SetActiveMode)
				.ToolTip(IDocumentation::Get()->CreateToolTip(
					LOCTEXT("ListingModeButtonTooltip", "Switch to Blueprint Listing Mode"),
					nullptr,
					TEXT("Shared/Editors/BlueprintEditor"),
					TEXT("ListingMode")))
				.IconImage(FAppStyle::GetBrush("BTEditor.Graph.NewTask"))
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ListingMode")))
			);

			BlueprintEditor->AddToolbarWidget(SNew(SSpacer).Size(FVector2D(10.0f, 1.0f)));

			BlueprintEditor->AddToolbarWidget(
				SNew(SModeWidget, FRenderPagesApplicationModes::GetLocalizedMode(FRenderPagesApplicationModes::LogicMode), FRenderPagesApplicationModes::LogicMode)
				.OnGetActiveMode(GetActiveMode)
				.OnSetActiveMode(SetActiveMode)
				.CanBeSelected(BlueprintEditor.Get(), &FBlueprintEditor::IsEditingSingleBlueprint)
				.ToolTip(IDocumentation::Get()->CreateToolTip(
					LOCTEXT("LogicModeButtonTooltip", "Switch to Logic Editing Mode"),
					nullptr,
					TEXT("Shared/Editors/BlueprintEditor"),
					TEXT("GraphMode")))
				.ToolTipText(LOCTEXT("LogicModeButtonTooltip", "Switch to Logic Editing Mode"))
				.IconImage(FAppStyle::GetBrush("Icons.Blueprint"))
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("LogicMode")))
			);

			// Right side padding
			BlueprintEditor->AddToolbarWidget(SNew(SSpacer).Size(FVector2D(10.0f, 1.0f)));
		}
	}
}


#undef LOCTEXT_NAMESPACE
