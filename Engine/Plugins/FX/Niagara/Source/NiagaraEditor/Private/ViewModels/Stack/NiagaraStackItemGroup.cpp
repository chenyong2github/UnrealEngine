// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"


void UNiagaraStackItemGroup::Initialize(FRequiredEntryData InRequiredEntryData, FText InDisplayName, FText InToolTip, INiagaraStackItemGroupAddUtilities* InAddUtilities)
{
	Super::Initialize(InRequiredEntryData, InDisplayName.ToString());
	GroupDisplayName = InDisplayName;
	GroupToolTip = InToolTip;
	AddUtilities = InAddUtilities;
	GroupFooter = nullptr;
}

FText UNiagaraStackItemGroup::GetDisplayName() const
{
	return GroupDisplayName;
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackItemGroup::GetStackRowStyle() const
{
	return EStackRowStyle::GroupHeader;
}

FText UNiagaraStackItemGroup::GetTooltipText() const 
{
	return GroupToolTip;
}

bool UNiagaraStackItemGroup::GetIsEnabled() const
{
	return OwningEmitterHandleViewModel.IsValid() == false || OwningEmitterHandleViewModel->GetIsEnabled();
}

INiagaraStackItemGroupAddUtilities* UNiagaraStackItemGroup::GetAddUtilities() const
{
	return AddUtilities;
}

uint32 UNiagaraStackItemGroup::GetRecursiveStackIssuesCount() const
{
	if (RecursiveStackIssuesCount.IsSet() == false)
	{
		TArray<UNiagaraStackErrorItem*> RecursiveIssues;
		FNiagaraStackGraphUtilities::GetStackIssuesRecursively(this, RecursiveIssues);
		RecursiveStackIssuesCount = RecursiveIssues.Num();
		EStackIssueSeverity MinSeverity = EStackIssueSeverity::Info;
		for (auto Issue : RecursiveIssues)
		{
			if (Issue->GetStackIssue().GetSeverity() < MinSeverity)
			{
				MinSeverity = Issue->GetStackIssue().GetSeverity();
			}
		}
		HighestIssueSeverity = MinSeverity;
	}
	return RecursiveStackIssuesCount.GetValue();
}

EStackIssueSeverity UNiagaraStackItemGroup::GetHighestStackIssueSeverity() const
{
	if (HighestIssueSeverity.IsSet() == false)
	{
		GetRecursiveStackIssuesCount();
	}
	return HighestIssueSeverity.GetValue();
}

void UNiagaraStackItemGroup::SetDisplayName(FText InDisplayName)
{
	GroupDisplayName = InDisplayName;
}

void UNiagaraStackItemGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (GroupFooter == nullptr)
	{
		GroupFooter = NewObject<UNiagaraStackItemGroupFooter>(this);
		GroupFooter->Initialize(FRequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), NAME_None, NAME_None, GetStackEditorData()));
	}

	NewChildren.Add(GroupFooter);

	RecursiveStackIssuesCount.Reset();
	HighestIssueSeverity.Reset();

	TSharedPtr<FNiagaraEmitterViewModel> OwningEmitterViewModel = GetEmitterViewModel();
	if (OwningEmitterViewModel.IsValid())
	{
		if (OwningEmitterHandleViewModel.IsValid() == false || OwningEmitterHandleViewModel->GetEmitterViewModel() != OwningEmitterViewModel)
		{
			OwningEmitterHandleViewModel = GetSystemViewModel()->GetEmitterHandleViewModelForEmitter(OwningEmitterViewModel->GetEmitter());
		}
	}
	else
	{
		OwningEmitterHandleViewModel.Reset();
	}
}

int32 UNiagaraStackItemGroup::GetChildIndentLevel() const
{
	return GetIndentLevel();
}

void UNiagaraStackItemGroup::ChlildStructureChangedInternal()
{
	Super::ChlildStructureChangedInternal();
	RecursiveStackIssuesCount.Reset();
	HighestIssueSeverity.Reset();
}

void UNiagaraStackItemGroupFooter::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, FString());
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackItemGroupFooter::GetStackRowStyle() const
{
	return UNiagaraStackEntry::EStackRowStyle::GroupFooter;
}

bool UNiagaraStackItemGroupFooter::GetCanExpand() const
{
	return false;
}
