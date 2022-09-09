// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/HierarchyEditor/NiagaraUserParametersHierarchyViewModel.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "SDropTarget.h"
#include "Customizations/NiagaraScriptVariableCustomization.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SToolTip.h"

#define LOCTEXT_NAMESPACE "NiagaraUserParametersHierarchyEditor"

void UNiagaraHierarchyUserParameter::Initialize(UNiagaraScriptVariable& InUserParameterScriptVariable, UNiagaraSystem& InSystem)
{
	UserParameterScriptVariable = &InUserParameterScriptVariable;
	SetGuid(InUserParameterScriptVariable.Metadata.GetVariableGuid());
	System = &InSystem;
}

void UNiagaraHierarchyUserParameter::RefreshDataInternal()
{	
	if(System == nullptr || FNiagaraEditorUtilities::FindScriptVariableForUserParameter(GetPersistentIdentity(), *System) == nullptr)
	{
		Finalize();
	}
}

TSharedRef<FNiagaraSystemViewModel> UNiagaraUserParametersHierarchyViewModel::GetSystemViewModel() const
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModelPinned = SystemViewModelWeak.Pin();
	checkf(SystemViewModelPinned.IsValid(), TEXT("System view model destroyed before user parameters hierarchy view model."));
	return SystemViewModelPinned.ToSharedRef();
}

void UNiagaraUserParametersHierarchyViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModelWeak = InSystemViewModel;
	UNiagaraHierarchyViewModelBase::Initialize();

	UNiagaraSystemEditorData& SystemEditorData = InSystemViewModel->GetEditorData();
	SystemEditorData.OnUserParameterScriptVariablesSynced().AddUObject(this, &UNiagaraUserParametersHierarchyViewModel::ForceFullRefresh);
}

void UNiagaraUserParametersHierarchyViewModel::FinalizeInternal()
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel = SystemViewModelWeak.Pin();
	if(SystemViewModel.IsValid())
	{
		GetSystemViewModel()->GetSystem().GetExposedParameters().RemoveAllOnChangedHandlers(this);
		UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(GetSystemViewModel()->GetSystem().GetEditorData());
		SystemEditorData->OnUserParameterScriptVariablesSynced().RemoveAll(this);
	}
}

TSharedRef<SWidget> FNiagaraUserParameterHierarchyDragDropOp::CreateCustomDecorator() const
{
	return FNiagaraParameterUtilities::GetParameterWidget(GetUserParameter(), true, false);
}

UNiagaraHierarchyRoot* UNiagaraUserParametersHierarchyViewModel::GetHierarchyDataRoot() const
{
	UNiagaraHierarchyRoot* RootItem = GetSystemViewModel()->GetEditorData().UserParameterHierarchy;

	ensure(RootItem != nullptr);
	return RootItem;
}

void UNiagaraUserParametersHierarchyViewModel::PrepareSourceItems()
{	
	TArray<FNiagaraVariable> UserParameters;
	GetSystemViewModel()->GetSystem().GetExposedParameters().GetUserParameters(UserParameters);

	TArray<UNiagaraHierarchyItemBase*> OldChildren = SourceRoot->GetChildrenMutable();
	SourceRoot->GetChildrenMutable().Empty();

	// we ensure we have one data child per user parameter, and we get rid of deleted ones
	for(FNiagaraVariable& UserParameter : UserParameters)
	{
		FNiagaraVariable RedirectedUserParameter = UserParameter;
		GetSystemViewModel()->GetSystem().GetExposedParameters().RedirectUserVariable(RedirectedUserParameter);
		TObjectPtr<UNiagaraScriptVariable> ScriptVariable = FNiagaraEditorUtilities::GetScriptVariableForUserParameter(RedirectedUserParameter, GetSystemViewModel());
		
		if(UNiagaraHierarchyItemBase** ExistingChild = OldChildren.FindByPredicate([ScriptVariable](UNiagaraHierarchyItemBase* ItemBase)
		{
			return ItemBase->GetPersistentIdentity() == ScriptVariable->Metadata.GetVariableGuid();
		}))
		{
			SourceRoot->GetChildrenMutable().Add(*ExistingChild);
			continue;
		}
		
		// since the source items are transient we need to create them here and keep them around until the end of the tool's lifetime
		UNiagaraHierarchyUserParameter* UserParameterHierarchyObject = NewObject<UNiagaraHierarchyUserParameter>(SourceRoot);
		UserParameterHierarchyObject->Initialize(*ScriptVariable.Get(), GetSystemViewModel()->GetSystem());
		SourceRoot->GetChildrenMutable().Add(UserParameterHierarchyObject);
	}

	// after we synced the data, we sync their view models that we display in the UI
	SourceViewModelRoot->SyncToData();
}

void UNiagaraUserParametersHierarchyViewModel::SetupCommands()
{
	// no custom commands yet
}

TOptional<EItemDropZone> UNiagaraUserParametersHierarchyViewModel::CanDropOn(TSharedPtr<FNiagaraHierarchyItemViewModelBase> SourceDropItem, TSharedPtr<FNiagaraHierarchyItemViewModelBase> TargetDropItem, EItemDropZone DropZone)
{
	bool bAllowDropOn =
		SourceDropItem->GetData() != TargetDropItem->GetData() &&
			(!SourceDropItem->HasParent(TargetDropItem, false) || DropZone != EItemDropZone::OntoItem)  &&
			!TargetDropItem->HasParent(SourceDropItem, true)
		&&
		(
			(SourceDropItem->GetData()->IsA<UNiagaraHierarchyUserParameter>() && TargetDropItem->GetData()->IsA<UNiagaraHierarchyCategory>() && DropZone == EItemDropZone::OntoItem)
				||
			(SourceDropItem->GetData()->IsA<UNiagaraHierarchyUserParameter>() && TargetDropItem->GetData()->IsA<UNiagaraHierarchyUserParameter>() && DropZone != EItemDropZone::OntoItem)
				||
			(SourceDropItem->GetData()->IsA<UNiagaraHierarchyCategory>() && TargetDropItem->GetData()->IsA<UNiagaraHierarchyCategory>()
				)
		);
	
	return bAllowDropOn ? DropZone : TOptional<EItemDropZone>();
}

TSharedRef<FNiagaraHierarchyDragDropOp> UNiagaraUserParametersHierarchyViewModel::CreateDragDropOp(TSharedRef<FNiagaraHierarchyItemViewModelBase> Item)
{
	if(const UNiagaraHierarchyUserParameter* UserParameter = Cast<UNiagaraHierarchyUserParameter>(Item->GetData()))
	{
		TSharedRef<FNiagaraUserParameterHierarchyDragDropOp> ParameterDragDropOp = MakeShared<FNiagaraUserParameterHierarchyDragDropOp>(Item);
		ParameterDragDropOp->Construct();
		return ParameterDragDropOp;
	}

	if(UNiagaraHierarchyCategory* HierarchyCategory = Cast<UNiagaraHierarchyCategory>(Item->GetDataMutable()))
	{
		TSharedRef<FNiagaraHierarchyDragDropOp> CategoryDragDropOp = MakeShared<FNiagaraHierarchyDragDropOp>(Item);
		CategoryDragDropOp->SetAdditionalLabel(FText::FromName(HierarchyCategory->GetCategoryName()));
		CategoryDragDropOp->Construct();
		return CategoryDragDropOp;
	}

	return MakeShared<FNiagaraHierarchyDragDropOp>(nullptr);
}

void UNiagaraUserParametersHierarchyViewModel::OnSelectionChanged(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem)
{
	if(!HierarchyItem.IsValid())
	{
		SelectObjectInDetailsPanelDelegate.Execute(nullptr);
		return;
	}

	UObject* ObjectToSelect = nullptr;
	if(HierarchyItem->GetData()->IsA<UNiagaraHierarchyUserParameter>())
	{
		UNiagaraHierarchyUserParameter* HierarchyUserParameter = Cast<UNiagaraHierarchyUserParameter>(HierarchyItem->GetDataMutable());
		FNiagaraVariable ContainedVariable = HierarchyUserParameter->GetUserParameter();
		GetSystemViewModel()->GetSystem().GetExposedParameters().RedirectUserVariable(ContainedVariable);
		TObjectPtr<UNiagaraScriptVariable> ScriptVariable = FNiagaraEditorUtilities::GetScriptVariableForUserParameter(ContainedVariable, GetSystemViewModel());

		if(ScriptVariable)
		{
			ObjectToSelect = ScriptVariable.Get();
		}
	}
	else
	{
		ObjectToSelect = HierarchyItem->GetDataMutable();
	}

	SelectObjectInDetailsPanelDelegate.Execute(ObjectToSelect);
}

TArray<TTuple<UClass*, FOnGetDetailCustomizationInstance>> UNiagaraUserParametersHierarchyViewModel::GetInstanceCustomizations()
{
	return {{UNiagaraScriptVariable::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraScriptVariableHierarchyDetails::MakeInstance)}};
}

TSharedRef<SWidget> UNiagaraUserParametersHierarchyViewModel::GenerateRowContentWidget(TSharedRef<FNiagaraHierarchyItemViewModelBase> ItemViewModelBase) const
{
	TSharedRef<SWidget> RowWidget = Super::GenerateRowContentWidget(ItemViewModelBase);

	if(RowWidget == SNullWidget::NullWidget)
	{
		if(const UNiagaraHierarchyUserParameter* UserParameter = Cast<UNiagaraHierarchyUserParameter>(ItemViewModelBase->GetData()))
		{
			RowWidget = FNiagaraParameterUtilities::GetParameterWidget(UserParameter->GetUserParameter(), true, false);
		}
	}	

	check(RowWidget != SNullWidget::NullWidget);
	return RowWidget;
}

#undef LOCTEXT_NAMESPACE
