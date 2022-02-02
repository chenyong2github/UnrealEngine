// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraScalabilityContext.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyRowGenerator.h"
#include "ISinglePropertyView.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraObjectSelection.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraScalabilityContext"

void SNiagaraScalabilityContext::Construct(const FArguments& InArgs, UNiagaraSystemScalabilityViewModel& InScalabilityViewModel)
{
	ScalabilityViewModel = &InScalabilityViewModel;
	
	ScalabilityViewModel->GetSystemViewModel().Pin()->GetSelectionViewModel()->OnEntrySelectionChanged().AddSP(this, &SNiagaraScalabilityContext::UpdateScalabilityContent);
	ScalabilityViewModel->GetSystemViewModel().Pin()->GetOverviewGraphViewModel()->GetNodeSelection()->OnSelectedObjectsChanged().AddSP(this, &SNiagaraScalabilityContext::UpdateScalabilityContent);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.ViewIdentifier = "ScalabilityContextDetailsView";
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ObjectsUseNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	// important: we register a generic details customization to overwrite the global customization for this object. This is because using "EditCategory" in a customization
	// puts a category from default into custom categories. This in turn will ignore the property visible delegate.
	// without this, a few properties from the mesh renderer properties make it into the details panel even though the delegate returns false for them.
	DetailsView->RegisterInstancedCustomPropertyLayout(UNiagaraMeshRendererProperties::StaticClass(), DetailsView->GetGenericLayoutDetailsDelegate());
	// add more here...
	
	DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SNiagaraScalabilityContext::FilterScalabilityProperties));
	UpdateScalabilityContent();

	ChildSlot
	[
		DetailsView.ToSharedRef()
	];
}

void SNiagaraScalabilityContext::SetObject(UObject* Object)
{	
	if(UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Object))
	{		
		DetailsView->SetObject(Emitter, true);
	}
	else if(UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
	{
		DetailsView->SetObject(System, true);
	}
	else if(UNiagaraStackRendererItem* RendererItem = Cast<UNiagaraStackRendererItem>(Object))
	{
		DetailsView->SetObject(RendererItem->GetRendererProperties(), true);
	}
}

void SNiagaraScalabilityContext::UpdateScalabilityContent()
{
	TArray<UNiagaraStackEntry*> StackEntries;
	ScalabilityViewModel->GetSystemViewModel().Pin()->GetSelectionViewModel()->GetSelectedEntries(StackEntries);
	const TSet<UObject*>& SelectedNodes = ScalabilityViewModel->GetSystemViewModel().Pin()->GetOverviewGraphViewModel()->GetNodeSelection()->GetSelectedObjects();

	if(StackEntries.Num() == 1)
	{
		UNiagaraStackEntry* StackEntry = StackEntries[0];
		bool bDoesSupportScalabilityContext =  StackEntry->IsA(UNiagaraStackRendererItem::StaticClass());

		if(bDoesSupportScalabilityContext)
		{
			SetObject(StackEntry);
			return;
		}
	}
	
	if(SelectedNodes.Num() > 0)
	{
		UObject* Object = SelectedNodes.Array()[0];
		UNiagaraOverviewNode* OverviewNode = CastChecked<UNiagaraOverviewNode>(Object);
		if(FNiagaraEmitterHandle* Handle = OverviewNode->TryGetEmitterHandle())
		{
			SetObject(Handle->GetInstance());
			return;
		}
		else
		{
			SetObject(OverviewNode->GetOwningSystem());
			return;
		}
	}
}

bool SNiagaraScalabilityContext::FilterScalabilityProperties(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeVisible = [](const FProperty& InProperty)
	{
		return InProperty.HasMetaData("DisplayInScalabilityContext");		
	};

	auto IsParentPropertyVisible = [&](const TArray<const FProperty*> ParentProperties)
	{
		for(const FProperty* Property : ParentProperties)
		{
			if(ShouldPropertyBeVisible(*Property))
			{
				return true;
			}
		}

		return false;
	};
	
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeVisible(**PropertyIt))
			{
				return true;
			}
		}
	}

	bool bShowProperty = ShouldPropertyBeVisible(InPropertyAndParent.Property);
	bShowProperty = bShowProperty || IsParentPropertyVisible(InPropertyAndParent.ParentProperties);
	return  bShowProperty;
}

#undef LOCTEXT_NAMESPACE
