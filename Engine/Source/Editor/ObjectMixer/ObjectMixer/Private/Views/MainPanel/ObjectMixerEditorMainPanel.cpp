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

void FObjectMixerEditorMainPanel::RebuildCategorySelector()
{
	MainPanelWidget->RebuildCategorySelector();
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

void FObjectMixerEditorMainPanel::AddObjectsToCategory(const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToAdd) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* Settings = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			Settings->AddObjectsToCategory(Filter->GetFName(), CategoryName, ObjectsToAdd);

			OnObjectMixerCategoryMapChanged.Broadcast();
		}
	}
}

void FObjectMixerEditorMainPanel::RemoveObjectsFromCategory(const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToRemove) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* Settings = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			Settings->RemoveObjectsFromCategory(Filter->GetFName(), CategoryName, ObjectsToRemove);

			OnObjectMixerCategoryMapChanged.Broadcast();
		}
	}
}

void FObjectMixerEditorMainPanel::RemoveCategory(const FName& CategoryName) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* Settings = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			Settings->RemoveCategory(Filter->GetFName(), CategoryName);

			OnObjectMixerCategoryMapChanged.Broadcast();
		}
	}
}

bool FObjectMixerEditorMainPanel::IsObjectInCategory(const FName& CategoryName, const FSoftObjectPath& InObject) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* Settings = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			return Settings->IsObjectInCategory(Filter->GetFName(), CategoryName, InObject);
		}
	}

	return false;
}

TSet<FName> FObjectMixerEditorMainPanel::GetCategoriesForObject(const FSoftObjectPath& InObject) const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* Settings = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			return Settings->GetCategoriesForObject(Filter->GetFName(), InObject);
		}
	}

	return {};
}

TSet<FName> FObjectMixerEditorMainPanel::GetAllCategories() const
{
	if (const TSubclassOf<UObjectMixerObjectFilter> Filter = GetObjectFilterClass())
	{
		if (UObjectMixerEditorSerializedData* Settings = GetMutableDefault<UObjectMixerEditorSerializedData>())
		{
			return Settings->GetAllCategories(Filter->GetFName());
		}
	}

	return {};
}

const TSet<FName>& FObjectMixerEditorMainPanel::GetCurrentCategorySelection() const
{
	return MainPanelWidget->GetCurrentCategorySelection();
}
