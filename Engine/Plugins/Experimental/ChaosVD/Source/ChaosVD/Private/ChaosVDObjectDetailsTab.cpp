// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVD/Public/ChaosVDObjectDetailsTab.h"

#include "ChaosVDStyle.h"
#include "ChaosVDTabsIDs.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Selection.h"
#include "Templates/SharedPointer.h"
#include "UnrealEd/Public/Editor.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

FChaosVDObjectDetailsTab::~FChaosVDObjectDetailsTab()
{
	if (GEditor == nullptr || GEditor->GetSelectedActors() == nullptr)
	{
		return;
	}

	GEditor->GetSelectedActors()->SelectionChangedEvent.Remove(SelectionDelegateHandle);
}

TSharedRef<SDockTab> FChaosVDObjectDetailsTab::HandleTabSpawned(const FSpawnTabArgs& Args)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	DetailsPanel = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	//TODO: This is just for testing. We will not use global selection events in the final version as these affect the entire editor
	SelectionDelegateHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddLambda(
	[this](UObject* Object)
	{
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

		UpdateSelectedObject(SelectedActors.Num() > 0 ? SelectedActors[0] : nullptr);
	} );

	TSharedRef<SDockTab> DetailsPanelTab =
		SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		.Label(LOCTEXT("DetailsPanel", "Details"))
		.ToolTipText(LOCTEXT("DetailsPanelToolTip", "See the details of the selected object"));
	DetailsPanelTab->SetContent
	(
		DetailsPanel.ToSharedRef()
	);

	DetailsPanelTab->SetTabIcon(FChaosVDStyle::Get().GetBrush("TabIconDetailsPanel"));

	return DetailsPanelTab;
}

void FChaosVDObjectDetailsTab::UpdateSelectedObject(AActor* NewObject) const
{
	DetailsPanel->SetObject(NewObject, true);
}

#undef LOCTEXT_NAMESPACE
