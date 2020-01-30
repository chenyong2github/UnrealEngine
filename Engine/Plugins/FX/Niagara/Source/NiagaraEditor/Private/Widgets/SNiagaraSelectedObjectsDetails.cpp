// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraSelectedObjectsDetails.h"
#include "NiagaraObjectSelection.h"

#include "Modules/ModuleManager.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "IDetailsView.h"

#define LOCTEXT_NAMESPACE "NiagaraSelectedObjectsDetails"

void SNiagaraSelectedObjectsDetails::Construct(const FArguments& InArgs, TSharedRef<FNiagaraObjectSelection> InSelectedObjects)
{
	SelectedObjectsArray.Push(InSelectedObjects);
	SelectedObjectsArray[0]->OnSelectedObjectsChanged().AddSP(this, &SNiagaraSelectedObjectsDetails::SelectedObjectsChanged);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::HideNameArea, true);
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObjects(SelectedObjectsArray[0]->GetSelectedObjects().Array());
	DetailsView->OnFinishedChangingProperties().AddRaw(this, &SNiagaraSelectedObjectsDetails::OnDetailsPanelFinishedChangingProperties);

	ChildSlot
	[
		DetailsView.ToSharedRef()
	];
} 

void SNiagaraSelectedObjectsDetails::Construct(const FArguments& InArgs, TSharedRef<FNiagaraObjectSelection> InSelectedObjects, TSharedRef<FNiagaraObjectSelection> InSelectedObjects2)
{
	SelectedObjectsArray.Push(InSelectedObjects);
	SelectedObjectsArray.Push(InSelectedObjects2);
	SelectedObjectsArray[0]->OnSelectedObjectsChanged().AddSP(this, &SNiagaraSelectedObjectsDetails::SelectedObjectsChanged);
	SelectedObjectsArray[1]->OnSelectedObjectsChanged().AddSP(this, &SNiagaraSelectedObjectsDetails::SelectedObjectsChangedSecond);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::HideNameArea, true);
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObjects(SelectedObjectsArray[0]->GetSelectedObjects().Array());
	DetailsView->OnFinishedChangingProperties().AddRaw(this, &SNiagaraSelectedObjectsDetails::OnDetailsPanelFinishedChangingProperties);

	ChildSlot
	[
		DetailsView.ToSharedRef()
	];
} 

void SNiagaraSelectedObjectsDetails::SelectedObjectsChanged()
{
	DetailsView->SetObjects(SelectedObjectsArray[0]->GetSelectedObjects().Array());
}

// TODO: Instead have a delegate that takes an array argument? This seems a bit dodgy..
void SNiagaraSelectedObjectsDetails::SelectedObjectsChangedSecond()
{
	DetailsView->SetObjects(SelectedObjectsArray[1]->GetSelectedObjects().Array());
}

void SNiagaraSelectedObjectsDetails::OnDetailsPanelFinishedChangingProperties(const FPropertyChangedEvent& InEvent)
{
	if (OnFinishedChangingPropertiesDelegate.IsBound())
	{
		OnFinishedChangingPropertiesDelegate.Broadcast(InEvent);
	}
}

#undef LOCTEXT_NAMESPACE // "NiagaraSelectedObjectsDetails"
