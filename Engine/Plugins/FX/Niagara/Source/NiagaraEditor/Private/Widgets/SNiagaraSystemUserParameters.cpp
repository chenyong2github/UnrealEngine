// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraSystemUserParameters.h"

#include "NiagaraConstants.h"
#include "NiagaraEditorData.h"
#include "NiagaraEditorUtilities.h"
#include "Customizations/NiagaraComponentDetails.h"
#include "Styling/StyleColors.h"
#include "SystemToolkitModes/NiagaraSystemToolkitModeBase.h"
#include "Modules/ModuleManager.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"
#include "Widgets/SNiagaraParameterMenu.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "NiagaraSystemUserParameters"

void SNiagaraSystemUserParameters::Construct(const FArguments& InArgs, TSharedPtr<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::HideNameArea;
	TSharedRef<IDetailsView> ObjectDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	ObjectDetailsView->RegisterInstancedCustomPropertyLayout(UNiagaraSystem::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraSystemUserParameterDetails::MakeInstance));
	ObjectDetailsView->SetObject(&SystemViewModel->GetSystem());
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(AddParameterButton, SComboButton)
				.OnGetMenuContent(this, &SNiagaraSystemUserParameters::GetParameterMenu)
				.OnComboBoxOpened_Lambda([this]
				{
					AddParameterButton->SetMenuContentWidgetToFocus(ParameterPanel->GetSearchBox());
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddUserParameterLabel", "Add Parameter"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &SNiagaraSystemUserParameters::SummonHierarchyEditor)
				.Text(LOCTEXT("SummonUserParametersHierarchyButtonLabel", "Edit Hierarchy"))
			]
		]
		+ SVerticalBox::Slot()
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("UserParameters")))
			[
				ObjectDetailsView
			]
		]
	];
}

TSharedRef<SWidget> SNiagaraSystemUserParameters::GetParameterMenu() 
{
	FNiagaraParameterPanelCategory UserCategory = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces({FNiagaraConstants::UserNamespace});
	ParameterPanel = SNew(SNiagaraAddParameterFromPanelMenu)
		.Graphs(SystemViewModel->GetParameterPanelViewModel()->GetEditableGraphsConst())
		.OnNewParameterRequested(this, &SNiagaraSystemUserParameters::AddParameter)
		.OnAllowMakeType(this, &SNiagaraSystemUserParameters::CanMakeNewParameterOfType)
		.NamespaceId(UserCategory.NamespaceMetaData.GetGuid())
		.ShowNamespaceCategory(false)
		.ShowGraphParameters(false)
		.AutoExpandMenu(false);
	
	return ParameterPanel.ToSharedRef();
}

void SNiagaraSystemUserParameters::AddParameter(FNiagaraVariable NewParameter) const
{
	FNiagaraParameterPanelCategory UserCategory = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces({FNiagaraConstants::UserNamespace});
	// TODO (ME) Change request rename to true when it's supported in the UI
	SystemViewModel->GetParameterPanelViewModel()->AddParameter(NewParameter, UserCategory, false, true);
}

bool SNiagaraSystemUserParameters::CanMakeNewParameterOfType(const FNiagaraTypeDefinition& InType) const
{
	return SystemViewModel->GetParameterPanelViewModel()->CanMakeNewParameterOfType(InType);
}

FReply SNiagaraSystemUserParameters::SummonHierarchyEditor()
{
	SystemViewModel->FocusTab(FNiagaraSystemToolkitModeBase::UserParametersHierarchyTabID);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
