// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLocalizationTargetEditor.h"
#include "Modules/ModuleManager.h"
#include "IDetailsView.h"
#include "LocalizationTargetTypes.h"
#include "LocalizationConfigurationScript.h"
#include "PropertyEditorModule.h"

void SLocalizationTargetEditor::Construct(const FArguments& InArgs, ULocalizationTargetSet* const InProjectSettings, ULocalizationTarget* const InLocalizationTarget, const FIsPropertyEditingEnabled& IsPropertyEditingEnabled)
{
	check(InProjectSettings->TargetObjects.Contains(InLocalizationTarget));
	LocalizationTarget = InLocalizationTarget;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	TSharedRef<IDetailsView> DetailsView = PropertyModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(InLocalizationTarget, true);
	DetailsView->SetIsPropertyEditingEnabledDelegate(IsPropertyEditingEnabled);
	DetailsView->OnFinishedChangingProperties().AddSP(this, &SLocalizationTargetEditor::OnFinishedChangingProperties);

	ChildSlot
	[
		DetailsView
	];
}

void SLocalizationTargetEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& InEvent)
{
	if (ULocalizationTarget* LocalizationTargetPtr = LocalizationTarget.Get())
	{
		// Update the exported gather INIs for this target to reflect the new settings
		LocalizationConfigurationScript::GenerateAllConfigFiles(LocalizationTargetPtr);
	}
}
