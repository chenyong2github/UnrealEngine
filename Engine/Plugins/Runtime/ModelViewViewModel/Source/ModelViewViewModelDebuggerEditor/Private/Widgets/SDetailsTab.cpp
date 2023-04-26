// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDetailsTab.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "MVVMDebuggerSDetailsTab"

namespace UE::MVVM
{

void SDetailsTab::Construct(const FArguments& InArgs)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bShowOptions = true;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.bAllowFavoriteSystem = true;
		DetailsViewArgs.bShowObjectLabel = false;
		DetailsViewArgs.bHideSelectionTip = true;
	}

	TSharedRef<IDetailsView> PropertyView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailView = PropertyView;

	ChildSlot
	[
		PropertyView
	];
}

void SDetailsTab::SetObjects(const TArray<UObject*>& InObjects)
{
	DetailView->SetObjects(InObjects);
}

} //namespace

#undef LOCTEXT_NAMESPACE