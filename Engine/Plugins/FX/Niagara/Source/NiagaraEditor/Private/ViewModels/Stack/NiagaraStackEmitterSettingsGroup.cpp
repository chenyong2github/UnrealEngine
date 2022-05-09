// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackEmitterSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackSummaryViewInputCollection.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitterDetailsCustomization.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraSystem.h"
#include "NiagaraEditorStyle.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Styling/AppStyle.h"
#include "IDetailTreeNode.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackEmitterItemGroup"

void UNiagaraStackEmitterPropertiesItem::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, TEXT("EmitterProperties"));
	Emitter = GetEmitterViewModel()->GetEmitter();
	Emitter->OnPropertiesChanged().AddUObject(this, &UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged);
}

void UNiagaraStackEmitterPropertiesItem::FinalizeInternal()
{
	if (Emitter.IsValid())
	{
		Emitter->OnPropertiesChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

FText UNiagaraStackEmitterPropertiesItem::GetDisplayName() const
{
	return LOCTEXT("EmitterPropertiesName", "Emitter Properties");
}

FText UNiagaraStackEmitterPropertiesItem::GetTooltipText() const
{
	return LOCTEXT("EmitterPropertiesTooltip", "Properties that are handled per Emitter. These cannot change at runtime.");
}

bool UNiagaraStackEmitterPropertiesItem::TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const
{
	if (bCanResetToBaseCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		if (BaseEmitter != nullptr && Emitter != BaseEmitter)
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			bCanResetToBaseCache = MergeManager->IsEmitterEditablePropertySetDifferentFromBase(*Emitter.Get(), *BaseEmitter);
		}
		else
		{
			bCanResetToBaseCache = false;
		}
	}
	if (bCanResetToBaseCache.GetValue())
	{
		OutCanResetToBaseMessage = LOCTEXT("CanResetToBase", "Reset the emitter properties to the state defined by the parent emitter.");
		return true;
	}
	else
	{
		OutCanResetToBaseMessage = LOCTEXT("CanNotResetToBase", "No parent to reset to, or not different from parent.");
		return false;
	}
}

void UNiagaraStackEmitterPropertiesItem::ResetToBase()
{
	FText Unused;
	if (TestCanResetToBaseWithMessage(Unused))
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
		MergeManager->ResetEmitterEditablePropertySetToBase(*Emitter, *BaseEmitter);
	}
}

const FSlateBrush* UNiagaraStackEmitterPropertiesItem::GetIconBrush() const
{
	if (Emitter->SimTarget == ENiagaraSimTarget::CPUSim)
	{
		return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.CPUIcon");
	}
	if (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.GPUIcon");
	}
	return FAppStyle::GetBrush("NoBrush");
}

void UNiagaraStackEmitterPropertiesItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (EmitterObject == nullptr)
	{
		EmitterObject = NewObject<UNiagaraStackObject>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), FExecutionCategoryNames::Emitter, NAME_None, GetStackEditorData());
		bool bIsTopLevelObject = true;
		EmitterObject->Initialize(RequiredEntryData, Emitter.Get(), bIsTopLevelObject, GetStackEditorDataKey());
		EmitterObject->RegisterInstancedCustomPropertyLayout(UNiagaraEmitter::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraEmitterDetails::MakeInstance));
	}

	NewChildren.Add(EmitterObject);
	bCanResetToBaseCache.Reset();
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
	RefreshIssues(NewIssues);
}


void UNiagaraStackEmitterPropertiesItem::RefreshIssues(TArray<FStackIssue>& NewIssues)
{
	UNiagaraEmitter* ActualEmitter = GetEmitterViewModel()->GetEmitter();
	if (ActualEmitter && ActualEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && ActualEmitter->CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Dynamic)
	{
		bool bAddError = true;
		
		UNiagaraSystem& Sys = GetSystemViewModel()->GetSystem();
		if (Sys.bFixedBounds)
		{
			bAddError = false;
		}			
		

		if (bAddError)
		{
			FStackIssue MissingRequiredFixedBoundsModuleError(
				EStackIssueSeverity::Warning,
				LOCTEXT("RequiredFixedBoundsWarningFormat", "The emitter is GPU and is using dynamic bounds mode.\r\nPlease update the Emitter or System properties otherwise bounds may be incorrect."),
				LOCTEXT("MissingFixedBounds", "Missing fixed bounds."),
				GetStackEditorDataKey(),
				false);

			NewIssues.Add(MissingRequiredFixedBoundsModuleError);
		}
	}
}

void UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged()
{
	if (IsFinalized() == false)
	{
		// Undo/redo can cause objects to disappear and reappear which can prevent safe removal of delegates
		// so guard against receiving an event when finalized here.
		bCanResetToBaseCache.Reset();
		RefreshChildren();

		if (Emitter.IsValid())
		{
			GetSystemViewModel()->GetEmitterHandleViewModelForEmitter(Emitter.Get()).Get()->GetEmitterStackViewModel()->RequestValidationUpdate();
		}
	}
}

void UNiagaraStackEmitterSummaryItem::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, TEXT("EmitterParameters"));
	Emitter = GetEmitterViewModel()->GetEmitter();
}

FText UNiagaraStackEmitterSummaryItem::GetDisplayName() const
{

	return LOCTEXT("EmitterSummaryName", "Emitter Summary");
}

FText UNiagaraStackEmitterSummaryItem::GetTooltipText() const
{

	return LOCTEXT("EmitterSummaryTooltip", "Subset of parameters from the stack, summarized here for easier access.");
}

FText UNiagaraStackEmitterSummaryItem::GetIconText() const
{
	return FText::FromString(FString(TEXT("\xf0ca")/* fa-list-ul */));
}

void UNiagaraStackEmitterSummaryItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (GetEmitterViewModel()->GetSummaryIsInEditMode())
	{
		if (SummaryEditorData == nullptr)
		{
			SummaryEditorData = NewObject<UNiagaraStackObject>(this);
			bool bIsTopLevelObject = true;
			SummaryEditorData->Initialize(CreateDefaultChildRequiredData(), &GetEmitterViewModel()->GetOrCreateEditorData(), bIsTopLevelObject, GetStackEditorDataKey());
			SummaryEditorData->SetOnSelectRootNodes(UNiagaraStackObject::FOnSelectRootNodes::CreateUObject(this,
				&UNiagaraStackEmitterSummaryItem::SelectSummaryNodesFromEmitterEditorDataRootNodes));
		}
		NewChildren.Add(SummaryEditorData);
	}
	else
	{
		SummaryEditorData = nullptr;
	}

	if (FilteredObject == nullptr)
	{
		FilteredObject = NewObject<UNiagaraStackSummaryViewObject>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), FExecutionCategoryNames::Emitter, NAME_None, GetStackEditorData());
		FilteredObject->Initialize(RequiredEntryData, Emitter.Get(), GetStackEditorDataKey());
	}

	NewChildren.Add(FilteredObject);
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

TSharedPtr<IDetailTreeNode> GetSummarySectionsPropertyNode(const TArray<TSharedRef<IDetailTreeNode>>& Nodes)
{
	TArray<TSharedRef<IDetailTreeNode>> ChildrenToCheck;
	for (TSharedRef<IDetailTreeNode> Node : Nodes)
	{
		if (Node->GetNodeType() == EDetailNodeType::Item)
		{
			TSharedPtr<IPropertyHandle> NodePropertyHandle = Node->CreatePropertyHandle();
			if (NodePropertyHandle.IsValid() && NodePropertyHandle->GetProperty()->GetFName() == UNiagaraEmitterEditorData::PrivateMemberNames::SummarySections)
			{
				return Node;
			}
		}

		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children);
		ChildrenToCheck.Append(Children);
	}
	if (ChildrenToCheck.Num() == 0)
	{
		return nullptr;
	}
	return GetSummarySectionsPropertyNode(ChildrenToCheck);
}

void UNiagaraStackEmitterSummaryItem::SelectSummaryNodesFromEmitterEditorDataRootNodes(TArray<TSharedRef<IDetailTreeNode>> Source, TArray<TSharedRef<IDetailTreeNode>>* Selected)
{
	TSharedPtr<IDetailTreeNode> SummarySectionsNode = GetSummarySectionsPropertyNode(Source);
	if (SummarySectionsNode.IsValid())
	{
		Selected->Add(SummarySectionsNode.ToSharedRef());
	}
}

bool UNiagaraStackEmitterSummaryItem::GetEditModeIsActive() const
{ 
	return GetEmitterViewModel().IsValid() && GetEmitterViewModel()->GetSummaryIsInEditMode();
}

void UNiagaraStackEmitterSummaryItem::SetEditModeIsActive(bool bInEditModeIsActive)
{
	if (GetEmitterViewModel().IsValid())
	{
		if (GetEmitterViewModel()->GetSummaryIsInEditMode() != bInEditModeIsActive)
		{
			GetEmitterViewModel()->SetSummaryIsInEditMode(bInEditModeIsActive);
			RefreshChildren();
		}
	}
}

UNiagaraStackEmitterSummaryGroup::UNiagaraStackEmitterSummaryGroup()
	: SummaryItem(nullptr)
{
}

void UNiagaraStackEmitterSummaryGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (SummaryItem == nullptr)
	{
		SummaryItem = NewObject<UNiagaraStackEmitterSummaryItem>(this);
		SummaryItem->Initialize(CreateDefaultChildRequiredData());
	}
	NewChildren.Add(SummaryItem);

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

FText UNiagaraStackEmitterSummaryGroup::GetIconText() const
{
	return FText::FromString(FString(TEXT("\xf0ca")/* fa-list-ul */));
}




void UNiagaraStackSummaryViewCollapseButton::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, TEXT("ShowAdvanced"));
}

FText UNiagaraStackSummaryViewCollapseButton::GetDisplayName() const
{
	return LOCTEXT("SummaryCollapseButtonDisplayName", "Show Advanced");
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackSummaryViewCollapseButton::GetStackRowStyle() const
{
	return EStackRowStyle::GroupHeader;
}

FText UNiagaraStackSummaryViewCollapseButton::GetTooltipText() const 
{
	return LOCTEXT("SummaryCollapseButtonTooltip", "Expand/Collapse detailed view.");
}

bool UNiagaraStackSummaryViewCollapseButton::GetIsEnabled() const
{
	return true;
}

void UNiagaraStackSummaryViewCollapseButton::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	
}








#undef LOCTEXT_NAMESPACE
