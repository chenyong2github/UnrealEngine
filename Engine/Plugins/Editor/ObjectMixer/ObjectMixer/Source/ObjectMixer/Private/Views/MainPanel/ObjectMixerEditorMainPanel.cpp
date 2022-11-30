// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "LevelEditorActions.h"
#include "ObjectMixerEditorSerializedData.h"
#include "Framework/Commands/GenericCommands.h"
#include "UObject/Package.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

void FObjectMixerEditorMainPanel::Initialize()
{
	RegenerateListModel();
	RegisterAndMapContextMenuCommands();
}

void FObjectMixerEditorMainPanel::RegisterAndMapContextMenuCommands()
{
	ObjectMixerElementEditCommands = MakeShared<FUICommandList>();

	ObjectMixerElementEditCommands->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("EDIT CUT") ) ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::Cut_CanExecute )
	);
	ObjectMixerElementEditCommands->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("EDIT COPY") ) ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::Copy_CanExecute )
	);
	ObjectMixerElementEditCommands->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("EDIT PASTE") ) ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::Paste_CanExecute )
	);
	ObjectMixerElementEditCommands->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("DUPLICATE") ) ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::Duplicate_CanExecute )
	);
	ObjectMixerElementEditCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("DELETE") ) ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::Delete_CanExecute )
	);
	ObjectMixerElementEditCommands->MapAction(FGenericCommands::Get().Rename,
		FUIAction(FExecuteAction::CreateRaw(this, &FObjectMixerEditorMainPanel::OnRenameCommand))
	);
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

void FObjectMixerEditorMainPanel::OnRenameCommand()
{
	if (EditorListModel.IsValid())
	{
		EditorListModel->OnRenameCommand();
	}
}

void FObjectMixerEditorMainPanel::OnRequestNewFolder(TOptional<FFolder> ExplicitParentFolder)
{
	if (EditorListModel.IsValid())
	{
		EditorListModel->OnRequestNewFolder(ExplicitParentFolder);
	}
}

void FObjectMixerEditorMainPanel::OnRequestMoveFolder(const FFolder& FolderToMove, const FFolder& TargetNewParentFolder)
{
	if (EditorListModel.IsValid())
	{
		EditorListModel->OnRequestMoveFolder(FolderToMove, TargetNewParentFolder);
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

void FObjectMixerEditorMainPanel::SetDefaultFilterClass(UClass* InNewClass)
{
	DefaultFilterClass = InNewClass;
	AddObjectFilterClass(InNewClass);
}

bool FObjectMixerEditorMainPanel::IsClassSelected(UClass* InClass) const
{
	return GetObjectFilterClasses().Contains(InClass);
}

const TArray<TObjectPtr<UObjectMixerObjectFilter>>& FObjectMixerEditorMainPanel::GetObjectFilterInstances()
{
	if (ObjectFilterInstances.Num() == 0)
	{
		CacheObjectFilterObjects();
	}

	return ObjectFilterInstances;
}

const UObjectMixerObjectFilter* FObjectMixerEditorMainPanel::GetMainObjectFilterInstance()
{
	return GetObjectFilterInstances().Num() > 0 ? GetObjectFilterInstances()[0].Get() : nullptr;
}

void FObjectMixerEditorMainPanel::CacheObjectFilterObjects()
{
	ObjectFilterInstances.Reset();
	
	for (const TSubclassOf<UObjectMixerObjectFilter> Class : GetObjectFilterClasses())
	{
		UObjectMixerObjectFilter* NewInstance = NewObject<UObjectMixerObjectFilter>(GetTransientPackage(), Class);
		ObjectFilterInstances.Add(TObjectPtr<UObjectMixerObjectFilter>(NewInstance));
	}
}

TSet<UClass*> FObjectMixerEditorMainPanel::GetObjectClassesToFilter()
{
	TSet<UClass*> ReturnValue;
	for (const TObjectPtr<UObjectMixerObjectFilter>& Filter : GetObjectFilterInstances())
	{
		ReturnValue.Append(Filter->GetObjectClassesToFilter());
	}
		
	return ReturnValue;
}

TSet<TSubclassOf<AActor>> FObjectMixerEditorMainPanel::GetObjectClassesToPlace()
{
	TSet<TSubclassOf<AActor>> ReturnValue;

	for (const TObjectPtr<UObjectMixerObjectFilter>& Filter : GetObjectFilterInstances())
	{
		ReturnValue.Append(Filter->GetObjectClassesToPlace());
	}
		
	return ReturnValue;
}

const TArray<TSharedRef<IObjectMixerEditorListFilter>>& FObjectMixerEditorMainPanel::GetListFilters() const
{
	return MainPanelWidget->GetListFilters();	
}

TArray<TWeakPtr<IObjectMixerEditorListFilter>> FObjectMixerEditorMainPanel::GetWeakActiveListFiltersSortedByName() const
{
	return MainPanelWidget->GetWeakActiveListFiltersSortedByName();
}

void FObjectMixerEditorMainPanel::AddObjectFilterClass(UClass* InObjectFilterClass, const bool bCacheAndRebuild)
{
	if (ensureAlwaysMsgf(InObjectFilterClass->IsChildOf(UObjectMixerObjectFilter::StaticClass()), TEXT("%hs: Class '%s' is not a child of UObjectMixerObjectFilter."), __FUNCTION__, *InObjectFilterClass->GetName()))
	{
		ObjectFilterClasses.Add(InObjectFilterClass);

		if (bCacheAndRebuild)
		{
			CacheAndRebuildFilters();
		}
	}
}

void FObjectMixerEditorMainPanel::RemoveObjectFilterClass(UClass* InObjectFilterClass, const bool bCacheAndRebuild)
{
	if (ObjectFilterClasses.Remove(InObjectFilterClass) > 0 && bCacheAndRebuild)
	{
		CacheAndRebuildFilters();
	}
}

UObjectMixerEditorSerializedData* FObjectMixerEditorMainPanel::GetSerializedData() const
{
	return GetMutableDefault<UObjectMixerEditorSerializedData>();
}

bool FObjectMixerEditorMainPanel::RequestAddObjectsToCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToAdd) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionName))
			{
				return SerializedData->AddObjectsToCollection(FilterName, CollectionName, ObjectsToAdd);
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::RequestRemoveObjectsFromCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToRemove) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionName))
			{
				return SerializedData->RemoveObjectsFromCollection(FilterName, CollectionName, ObjectsToRemove);
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::RequestRemoveCollection(const FName& CollectionName) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionName))
			{
				return SerializedData->RemoveCollection(FilterName, CollectionName);
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::RequestDuplicateCollection(const FName& CollectionToDuplicateName, FName& DesiredDuplicateName) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionToDuplicateName))
			{
				return SerializedData->DuplicateCollection(FilterName, CollectionToDuplicateName, DesiredDuplicateName);
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::RequestReorderCollection(const FName& CollectionToMoveName, const FName& CollectionInsertBeforeName) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionToMoveName))
			{
				return SerializedData->ReorderCollection(FilterName, CollectionToMoveName, CollectionInsertBeforeName);
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::RequestRenameCollection(const FName& CollectionNameToRename,
	const FName& NewCollectionName) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionNameToRename))
			{
				return SerializedData->RenameCollection(FilterName, CollectionNameToRename, NewCollectionName);
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::DoesCollectionExist(const FName& CollectionName) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionName))
			{
				return true;
			}
		}
	}

	return false;
}

bool FObjectMixerEditorMainPanel::IsObjectInCollection(const FName& CollectionName, const FSoftObjectPath& InObject) const
{
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			if (SerializedData->DoesCollectionExist(FilterName, CollectionName))
			{
				return SerializedData->IsObjectInCollection(FilterName, CollectionName, InObject);
			}
		}
	}

	return false;
}

TSet<FName> FObjectMixerEditorMainPanel::GetCollectionsForObject(const FSoftObjectPath& InObject) const
{
	TSet<FName> ReturnValue;
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			ReturnValue.Append(SerializedData->GetCollectionsForObject(FilterName, InObject));
		}
	}

	return ReturnValue;
}

TArray<FName> FObjectMixerEditorMainPanel::GetAllCollectionNames() const
{
	TArray<FName> ReturnValue;
	if (UObjectMixerEditorSerializedData* SerializedData = GetSerializedData())
	{
		for (const TSubclassOf<UObjectMixerObjectFilter>& Class : GetObjectFilterClasses())
		{
			const FName FilterName = Class->GetFName();
			ReturnValue.Append(SerializedData->GetAllCollectionNames(FilterName));
		}
	}

	return ReturnValue;
}

TSet<TSharedRef<FObjectMixerEditorListFilter_Collection>> FObjectMixerEditorMainPanel::GetCurrentCollectionSelection() const
{
	return MainPanelWidget->GetCurrentCollectionSelection();
}

const TSubclassOf<UObjectMixerObjectFilter>& FObjectMixerEditorMainPanel::GetDefaultFilterClass() const
{
	return DefaultFilterClass;
}
