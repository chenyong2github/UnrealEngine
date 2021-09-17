// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerVizSettingsTabSummoner.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorData.h"
#include "MLDeformerEditorStyle.h"

#include "IDocumentation.h"
#include "SScrubControlPanel.h"

#define LOCTEXT_NAMESPACE "MLDeformerVizSettingsTabSummoner"

const FName FMLDeformerVizSettingsTabSummoner::TabID(TEXT("MLDeformerVizSettings"));

FMLDeformerVizSettingsTabSummoner::FMLDeformerVizSettingsTabSummoner(const TSharedRef<FMLDeformerEditorToolkit>& InEditor)
	: FWorkflowTabFactory(TabID, InEditor)
	, Editor(InEditor)
{
	bIsSingleton = true; // only allow a single instance of this tab

	TabLabel = LOCTEXT("VizSettingsTabLabel", "Visualization");
	TabIcon = FSlateIcon(FMLDeformerEditorStyle::Get().GetStyleSetName(), "MLDeformer.VizSettings.TabIcon");
	ViewMenuDescription = LOCTEXT("ViewMenu_Desc", "Visualization Settings");
	ViewMenuTooltip = LOCTEXT("ViewMenu_ToolTip", "Show the ML Deformer Debug Visualization Settings");

	// Create details view.
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	Editor.Pin()->GetEditorData()->SetVizSettingsDetailsView(DetailsView);
}

TSharedPtr<SToolTip> FMLDeformerVizSettingsTabSummoner::CreateTabToolTipWidget(const FWorkflowTabSpawnInfo& Info) const
{
	return IDocumentation::Get()->CreateToolTip(
		LOCTEXT("VizSettingsTooltip", "The visualization settings for the ML Deformer."), 
		NULL, 
		TEXT("Shared/Editors/Persona"), 
		TEXT("MLDeformerVizSettings_Window"));
}

TSharedRef<SWidget> FMLDeformerVizSettingsTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
		.FillHeight(1.0f)
		[
			DetailsView.ToSharedRef()
		];

	// Init the viz settings details panel.
	UMLDeformerVizSettings* VizSettings = Editor.Pin()->GetEditorData()->GetDeformerAsset()->VizSettings;
	Editor.Pin()->GetEditorData()->GetVizSettingsDetailsView()->OnFinishedChangingProperties().AddSP(Editor.Pin().ToSharedRef(), &FMLDeformerEditorToolkit::OnFinishedChangingDetails);
	Editor.Pin()->GetEditorData()->GetVizSettingsDetailsView()->SetObject(VizSettings);

	return Content;
}

#undef LOCTEXT_NAMESPACE 
