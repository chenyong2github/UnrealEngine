// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackItemFooter.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "NiagaraStackEditorData.h"

void UNiagaraStackItem::Initialize(FRequiredEntryData InRequiredEntryData, FString InStackEditorDataKey)
{
	Super::Initialize(InRequiredEntryData, InStackEditorDataKey);
	AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackItem::FilterAdvancedChildren));
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackItem::GetStackRowStyle() const
{
	return UNiagaraStackEntry::EStackRowStyle::ItemHeader;
}

UNiagaraStackItem::FOnModifiedGroupItems& UNiagaraStackItem::OnModifiedGroupItems()
{
	return ModifiedGroupItemsDelegate;
}

uint32 UNiagaraStackItem::GetRecursiveStackIssuesCount() const
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

EStackIssueSeverity UNiagaraStackItem::GetHighestStackIssueSeverity() const
{
	if (HighestIssueSeverity.IsSet() == false)
	{
		GetRecursiveStackIssuesCount();
	}
	return HighestIssueSeverity.GetValue();
}

void UNiagaraStackItem::SetIsEnabled(bool bInIsEnabled)
{
	if (ItemFooter != nullptr)
	{
		ItemFooter->SetIsEnabled(bInIsEnabled);
	}
	SetIsEnabledInternal(bInIsEnabled);
}

void UNiagaraStackItem::Delete()
{
	if (GetDisplayedObject() != nullptr)
	{
		GetSystemViewModel()->GetSelectionViewModel()->RemoveEntryFromSelectionByDisplayedObject(GetDisplayedObject());
	}
	DeleteInternal();
}

void UNiagaraStackItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (ItemFooter == nullptr)
	{
		ItemFooter = NewObject<UNiagaraStackItemFooter>(this);
		ItemFooter->Initialize(CreateDefaultChildRequiredData(), GetStackEditorDataKey());
		ItemFooter->SetOnToggleShowAdvanced(UNiagaraStackItemFooter::FOnToggleShowAdvanced::CreateUObject(this, &UNiagaraStackItem::ToggleShowAdvanced));
	}
	ItemFooter->SetIsEnabled(GetIsEnabled());

	NewChildren.Add(ItemFooter);
	RecursiveStackIssuesCount.Reset();
	HighestIssueSeverity.Reset();
}

void GetContentChildren(UNiagaraStackEntry& CurrentEntry, TArray<UNiagaraStackItemContent*>& ContentChildren)
{
	TArray<UNiagaraStackEntry*> Children;
	CurrentEntry.GetUnfilteredChildren(Children);
	for (UNiagaraStackEntry* Child : Children)
	{
		UNiagaraStackItemContent* ContentChild = Cast<UNiagaraStackItemContent>(Child);
		if (ContentChild != nullptr)
		{
			ContentChildren.Add(ContentChild);
		}
		GetContentChildren(*Child, ContentChildren);
	}
}

void UNiagaraStackItem::PostRefreshChildrenInternal()
{
	Super::PostRefreshChildrenInternal();
	bool bHasAdvancedContent = false;
	TArray<UNiagaraStackItemContent*> ContentChildren;
	GetContentChildren(*this, ContentChildren);
	for (UNiagaraStackItemContent* ContentChild : ContentChildren)
	{
		if (ContentChild->GetIsAdvanced())
		{
			bHasAdvancedContent = true;
			break;
		}
	}
	ItemFooter->SetHasAdvancedContent(bHasAdvancedContent);
}

int32 UNiagaraStackItem::GetChildIndentLevel() const
{
	return GetIndentLevel();
}

void UNiagaraStackItem::ChlildStructureChangedInternal()
{
	Super::ChlildStructureChangedInternal();
	RecursiveStackIssuesCount.Reset();
	HighestIssueSeverity.Reset();
}

bool UNiagaraStackItem::FilterAdvancedChildren(const UNiagaraStackEntry& Child) const
{
	const UNiagaraStackItemContent* ItemContent = Cast<UNiagaraStackItemContent>(&Child);
	if (ItemContent == nullptr || ItemContent->GetIsAdvanced() == false)
	{
		return true;
	}
	else
	{
		return GetStackEditorData().GetShowAllAdvanced() || GetStackEditorData().GetStackItemShowAdvanced(GetStackEditorDataKey(), false);
	}
}

void UNiagaraStackItem::ToggleShowAdvanced()
{
	bool bCurrentShowAdvanced = GetStackEditorData().GetStackItemShowAdvanced(GetStackEditorDataKey(), false);
	GetStackEditorData().SetStackItemShowAdvanced(GetStackEditorDataKey(), !bCurrentShowAdvanced);
	OnStructureChanged().Broadcast();
}

void UNiagaraStackItemContent::Initialize(FRequiredEntryData InRequiredEntryData, bool bInIsAdvanced, FString InOwningStackItemEditorDataKey, FString InStackEditorDataKey)
{
	Super::Initialize(InRequiredEntryData, InStackEditorDataKey);
	OwningStackItemEditorDataKey = InOwningStackItemEditorDataKey;
	bIsAdvanced = bInIsAdvanced;
	AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackItemContent::FilterAdvancedChildren));
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackItemContent::GetStackRowStyle() const
{
	return bIsAdvanced ? EStackRowStyle::ItemContentAdvanced : EStackRowStyle::ItemContent;
}

bool UNiagaraStackItemContent::GetIsAdvanced() const
{
	return bIsAdvanced;
}

FString UNiagaraStackItemContent::GetOwnerStackItemEditorDataKey() const
{
	return OwningStackItemEditorDataKey;
}

void UNiagaraStackItemContent::SetIsAdvanced(bool bInIsAdvanced)
{
	if (bIsAdvanced != bInIsAdvanced)
	{
		// When changing advanced, invalidate the structure so that the filters run again.
		bIsAdvanced = bInIsAdvanced;
		OnStructureChanged().Broadcast();
	}
}

bool UNiagaraStackItemContent::FilterAdvancedChildren(const UNiagaraStackEntry& Child) const
{
	const UNiagaraStackItemContent* ItemContent = Cast<UNiagaraStackItemContent>(&Child);
	if (ItemContent == nullptr || ItemContent->GetIsAdvanced() == false || ItemContent->GetIsSearchResult())
	{
		return true;
	}
	else
	{
		return GetStackEditorData().GetShowAllAdvanced() || GetStackEditorData().GetStackItemShowAdvanced(OwningStackItemEditorDataKey, false);
	}
}
