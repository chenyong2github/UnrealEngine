// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "ObjectMixerEditorSerializedData.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

void FObjectMixerEditorMainPanel::Init()
{
	RegenerateListModel();
}

TSharedRef<SWidget> FObjectMixerEditorMainPanel::GetOrCreateWidget()
{
	if (!MainPanelWidget.IsValid())
	{
		SAssignNew(MainPanelWidget, SObjectMixerEditorMainPanel, SharedThis(this));
	}

	return MainPanelWidget.ToSharedRef();
}

void FObjectMixerEditorMainPanel::RegenerateListModel()
{
	EditorListModel.Reset();
	
	EditorListModel = MakeShared<FObjectMixerEditorList>(SharedThis(this));
}

void FObjectMixerEditorMainPanel::RequestRebuildList() const
{
	if (EditorListModel.IsValid())
	{
		EditorListModel->RequestRebuildList();
	}
}

void FObjectMixerEditorMainPanel::RefreshList() const
{
	if (EditorListModel.IsValid())
	{
		EditorListModel->RefreshList();
	}
}

void FObjectMixerEditorMainPanel::RequestSyncEditorSelectionToListSelection() const
{
	if (EditorListModel.IsValid())
	{
		EditorListModel->RequestSyncEditorSelectionToListSelection();
	}
}

void FObjectMixerEditorMainPanel::RebuildCollectionSelector()
{
	MainPanelWidget->RebuildCollectionSelector();
}

FText FObjectMixerEditorMainPanel::GetSearchTextFromSearchInputField() const
{
	return MainPanelWidget->GetSearchTextFromSearchInputField();
}

FString FObjectMixerEditorMainPanel::GetSearchStringFromSearchInputField() const
{
	return MainPanelWidget->GetSearchStringFromSearchInputField();
}

void FObjectMixerEditorMainPanel::OnClassSelectionChanged(UClass* InNewClass)
{
	SetObjectFilterClass(InNewClass);
}

TObjectPtr<UClass> FObjectMixerEditorMainPanel::GetClassSelection() const
{
	return GetObjectFilterClass();
}

bool FObjectMixerEditorMainPanel::IsClassSelected(UClass* InNewClass) const
{
	return InNewClass == GetClassSelection();
}

UObjectMixerObjectFilter* FObjectMixerEditorMainPanel::GetObjectFilter()
{
	if (!ObjectFilterPtr.IsValid())
	{
		CacheObjectFilterObject();
	}

	return ObjectFilterPtr.Get();
}

void FObjectMixerEditorMainPanel::CacheObjectFilterObject()
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

const TArray<TSharedRef<IObjectMixerEditorListFilter>>& FObjectMixerEditorMainPanel::GetShowFilters() const
{
	return MainPanelWidget->GetShowFilters();	
}

void FObjectMixerEditorMainPanel::AddObjectsToCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToAdd) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			SerializedData->AddObjectsToCollection(Filter->GetFName(), CollectionName, ObjectsToAdd);

			OnObjectMixerCollectionMapChanged.Broadcast();
		}
	}
}

void FObjectMixerEditorMainPanel::RemoveObjectsFromCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToRemove) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			SerializedData->RemoveObjectsFromCollection(Filter->GetFName(), CollectionName, ObjectsToRemove);

			OnObjectMixerCollectionMapChanged.Broadcast();
		}
	}
}

void FObjectMixerEditorMainPanel::RemoveCollection(const FName& CollectionName) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			SerializedData->RemoveCollection(Filter->GetFName(), CollectionName);

			OnObjectMixerCollectionMapChanged.Broadcast();
		}
	}
}

void FObjectMixerEditorMainPanel::ReorderCollection(const FName& CollectionToMoveName, const FName& CollectionInsertBeforeName) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			SerializedData->ReorderCollection(Filter->GetFName(), CollectionToMoveName, CollectionInsertBeforeName);

			OnObjectMixerCollectionMapChanged.Broadcast();
		}
	}
}

bool FObjectMixerEditorMainPanel::IsObjectInCollection(const FName& CollectionName, const FSoftObjectPath& InObject) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			return SerializedData->IsObjectInCollection(Filter->GetFName(), CollectionName, InObject);
		}
	}

	return false;
}

TSet<FName> FObjectMixerEditorMainPanel::GetCollectionsForObject(const FSoftObjectPath& InObject) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			return SerializedData->GetCollectionsForObject(Filter->GetFName(), InObject);
		}
	}

	return {};
}

TArray<FName> FObjectMixerEditorMainPanel::GetAllCollectionNames() const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			return SerializedData->GetAllCollectionNames(Filter->GetFName());
		}
	}

	return {};
}

const TSet<FName>& FObjectMixerEditorMainPanel::GetCurrentCollectionSelection() const
{
	return MainPanelWidget->GetCurrentCollectionSelection();
}
