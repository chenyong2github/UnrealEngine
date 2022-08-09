// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerEditorList.h"

#include "Views/List/SObjectMixerEditorList.h"

#include "UObject/UObjectGlobals.h"

FObjectMixerEditorList::~FObjectMixerEditorList()
{
	ListWidget.Reset();

	if (ObjectFilterPtr.IsValid())
	{
		ObjectFilterPtr.Reset();
	}
}

void FObjectMixerEditorList::FlushWidget()
{
	ListWidget.Reset();
}

TSharedRef<SWidget> FObjectMixerEditorList::GetOrCreateWidget()
{
	if (!ListWidget.IsValid())
	{
		SAssignNew(ListWidget, SObjectMixerEditorList, SharedThis(this));
	}

	RequestRebuildList();

	return ListWidget.ToSharedRef();
}

UObjectMixerObjectFilter* FObjectMixerEditorList::GetObjectFilter()
{
	if (!ObjectFilterPtr.IsValid())
	{
		CacheObjectFilterObject();
	}

	return ObjectFilterPtr.Get();
}

void FObjectMixerEditorList::CacheObjectFilterObject()
{
	if (ObjectFilterPtr.IsValid())
	{
		ObjectFilterPtr.Reset();
	}
	
	if (const UClass* Class = GetObjectFilterClass())
	{
		ObjectFilterPtr = TStrongObjectPtr(NewObject<UObjectMixerObjectFilter>(GetTransientPackage(), Class));
	}
}

void FObjectMixerEditorList::SetSearchString(const FString& SearchString)
{
	ListWidget->SetSearchStringInSearchInputField(SearchString);
}

void FObjectMixerEditorList::ClearList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->ClearList();
	}
}

void FObjectMixerEditorList::RequestRebuildList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RequestRebuildList();
	}
}

void FObjectMixerEditorList::RefreshList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RefreshList();
	}
}

bool FObjectMixerEditorList::IsClassSelected(UClass* InNewClass) const
{
	return InNewClass == GetObjectFilterClass();
}
