// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerEditorList.h"

#include "Views/List/SObjectMixerEditorList.h"

#include "Editor.h"

FObjectMixerEditorList::FObjectMixerEditorList()
{
	EditorDelegateHandles.Add(FEditorDelegates::MapChange.AddLambda([this](uint32 Index)
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::OnMapOpened.AddLambda([this](const FString& Filename, bool bAsTemplate)
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::ActorPropertiesChange.AddLambda([this]()
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::OnDeleteActorsEnd.AddLambda([this]()
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::OnApplyObjectToActor.AddLambda([this](UObject* Object, AActor* Actor)
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::OnDuplicateActorsEnd.AddLambda([this]()
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::OnNewActorsDropped.AddLambda([this](const TArray<UObject*>& Objects, const TArray<AActor*>& Actors)
	{
		RebuildList();
	}));
	EditorDelegateHandles.Add(FEditorDelegates::OnNewActorsPlaced.AddLambda([this](UObject* Object, const TArray<AActor*>& Actors)
	{
		RebuildList();
	}));
}

FObjectMixerEditorList::~FObjectMixerEditorList()
{
	ListWidget.Reset();

	// Unbind Delegates
	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::OnMapOpened.RemoveAll(this);
	FEditorDelegates::ActorPropertiesChange.RemoveAll(this);
	FEditorDelegates::OnDeleteActorsEnd.RemoveAll(this);
	FEditorDelegates::OnApplyObjectToActor.RemoveAll(this);
	FEditorDelegates::OnDuplicateActorsEnd.RemoveAll(this);
	FEditorDelegates::OnNewActorsDropped.RemoveAll(this);
	FEditorDelegates::OnNewActorsPlaced.RemoveAll(this);

	for (FDelegateHandle Delegate : EditorDelegateHandles)
	{
		Delegate.Reset();
	}
	EditorDelegateHandles.Empty();

	if (ObjectFilterPtr.IsValid())
	{
		ObjectFilterPtr.Get()->RemoveFromRoot();
	}
}

TSharedRef<SWidget> FObjectMixerEditorList::GetOrCreateWidget()
{
	if (!ListWidget.IsValid())
	{
		SAssignNew(ListWidget, SObjectMixerEditorList, SharedThis(this));
	}

	RebuildList();

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
		ObjectFilterPtr->RemoveFromRoot();
		ObjectFilterPtr = nullptr;
	}
	
	if (const UClass* Class = GetObjectFilterClass())
	{
		ObjectFilterPtr = NewObject<UObjectMixerObjectFilter>(GetTransientPackage(), Class);
		ObjectFilterPtr.Get()->AddToRoot();
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

void FObjectMixerEditorList::RebuildList(const FString& InItemToScrollTo) const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RebuildList(InItemToScrollTo);
	}
}

void FObjectMixerEditorList::RefreshList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RefreshList();
	}
}
