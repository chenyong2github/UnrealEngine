// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerEditorListRowData.h"

#include "ObjectMixerEditorLog.h"
#include "Views/List/ObjectMixerUtils.h"
#include "Views/List/SObjectMixerEditorList.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

const TArray<TObjectPtr<UObjectMixerObjectFilter>>& FObjectMixerEditorListRowData::GetObjectFilterInstances() const
{
	SObjectMixerEditorList* ListView = GetListView();
	check (ListView)

	const TSharedPtr<FObjectMixerEditorList> PinnedListModel = ListView->GetListModelPtr().Pin();
	check (PinnedListModel);
	
	return PinnedListModel->GetObjectFilterInstances();
}

const UObjectMixerObjectFilter* FObjectMixerEditorListRowData::GetMainObjectFilterInstance() const
{
	if (SObjectMixerEditorList* ListView = GetListView())
	{
		if (const TSharedPtr<FObjectMixerEditorList> PinnedListModel = ListView->GetListModelPtr().Pin())
		{
			return PinnedListModel->GetMainObjectFilterInstance();
		}
	}
	
	return nullptr;
}

bool FObjectMixerEditorListRowData::GetIsTreeViewItemExpanded(const TSharedRef<ISceneOutlinerTreeItem> InRow)
{
	return GetListView()->IsTreeViewItemExpanded(InRow);
}

void FObjectMixerEditorListRowData::SetIsTreeViewItemExpanded(const TSharedRef<ISceneOutlinerTreeItem> InRow, const bool bNewExpanded)
{
	GetListView()->SetTreeViewItemExpanded(InRow, bNewExpanded);
}

bool FObjectMixerEditorListRowData::GetIsSelected(const TSharedRef<ISceneOutlinerTreeItem> InRow)
{
	if (SObjectMixerEditorList* ListView = GetListView())
	{
		return ListView->IsTreeViewItemSelected(InRow);
	}

	return false;
}

void FObjectMixerEditorListRowData::SetIsSelected(const TSharedRef<ISceneOutlinerTreeItem> InRow, const bool bNewSelected)
{
	if (SObjectMixerEditorList* ListView = GetListView())
	{
		return ListView->SetTreeViewItemSelected(InRow, bNewSelected);
	}
}

bool FObjectMixerEditorListRowData::HasAtLeastOneChildThatIsNotSolo(const TSharedRef<ISceneOutlinerTreeItem> InRow, const bool bRecursive) const
{
	for (const TWeakPtr<ISceneOutlinerTreeItem>& ChildRow : InRow->GetChildren())
	{
		if (const TSharedPtr<ISceneOutlinerTreeItem> PinnedChildRow = ChildRow.Pin())
		{
			if (!FObjectMixerUtils::GetRowData(PinnedChildRow)->GetRowSoloState())
			{
				return true;
			}

			if (bRecursive && FObjectMixerUtils::GetRowData(PinnedChildRow)->HasAtLeastOneChildThatIsNotSolo(PinnedChildRow.ToSharedRef(), true))
			{
				return true;
			}
		}
	}

	return false;
}

FText FObjectMixerEditorListRowData::GetDisplayName(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem) const
{
	const FText Override = GetDisplayNameOverride();
	if (!Override.IsEmpty())
	{
		return Override;
	}

	if (const UObjectMixerObjectFilter* Filter = GetMainObjectFilterInstance())
	{
		if (const TObjectPtr<UObject> Object = FObjectMixerUtils::GetRowObject(InTreeItem))
		{
			return Filter->GetRowDisplayName(Object, GetIsHybridRow());
		}
	}

	return FText::GetEmpty();
}

SObjectMixerEditorList* FObjectMixerEditorListRowData::GetListView() const
{
	return StaticCast<SObjectMixerEditorList*>(SceneOutlinerPtr);
}

TArray<TSharedPtr<ISceneOutlinerTreeItem>> FObjectMixerEditorListRowData::GetSelectedTreeViewItems() const
{
	if (SObjectMixerEditorList* ListView = GetListView())
	{
		return ListView->GetSelectedTreeViewItems();
	}

	return {};
}

void FObjectMixerEditorListRowData::OnChangeVisibility(const FSceneOutlinerTreeItemRef TreeItem, const bool bNewVisible)
{
	if (FObjectMixerEditorListRowData* RowData = FObjectMixerUtils::GetRowData(TreeItem))
	{
		if (SObjectMixerEditorList* ListView = RowData->GetListView())
		{
			ListView->ClearSoloRows();
		}

		RowData->SetUserHiddenInEditor(!bNewVisible);
	}
}

bool FObjectMixerEditorListRowData::IsUserSetHiddenInEditor() const
{
	return VisibilityRules.bShouldBeHiddenInEditor;
}

void FObjectMixerEditorListRowData::SetUserHiddenInEditor(const bool bNewHidden)
{
	VisibilityRules.bShouldBeHiddenInEditor = bNewHidden;
}

bool FObjectMixerEditorListRowData::GetRowSoloState() const
{
	return VisibilityRules.bShouldBeSolo;
}

void FObjectMixerEditorListRowData::SetRowSoloState(const bool bNewSolo)
{
	VisibilityRules.bShouldBeSolo = bNewSolo;
}

void FObjectMixerEditorListRowData::ClearSoloRows() const
{
	if (SObjectMixerEditorList* ListView = GetListView())
	{
		ListView->ClearSoloRows();
	}
}

void SetValueOnSelectedItems(
	const FString& ValueAsString, const TArray<TSharedPtr<ISceneOutlinerTreeItem>>& OtherSelectedItems,
	const FName& PropertyName, const TSharedPtr<ISceneOutlinerTreeItem> PinnedItem,
	const EPropertyValueSetFlags::Type InFlags)
{
	if (!ValueAsString.IsEmpty())
	{
		const EPropertyValueSetFlags::Type Flags = InFlags | EPropertyValueSetFlags::NotTransactable;
		const bool bShouldTransact = InFlags & EPropertyValueSetFlags::DefaultFlags;

		if (bShouldTransact && GEditor->CanTransact())
		{
			GEditor->BeginTransaction(
			   NSLOCTEXT("ObjectMixerEditor","OnPropertyChangedTransaction", "Object Mixer - Edit Selected Row Properties"));
		}
		
		for (const TSharedPtr<ISceneOutlinerTreeItem>& SelectedRow : OtherSelectedItems)
		{
			if (SelectedRow == PinnedItem)
			{
				continue;
			}

			FObjectMixerEditorListRowData* RowData = FObjectMixerUtils::GetRowData(SelectedRow);
			if (!RowData)
			{
				continue;
			}

			// Skip folders
			if (FObjectMixerUtils::AsFolderRow(SelectedRow))
			{
				continue;
			}

			UObject* ObjectToModify = FObjectMixerUtils::GetRowObject(SelectedRow, true);
			
			if (IsValid(ObjectToModify))
			{
				ObjectToModify->Modify();
			}
			else
			{
				UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: Row '%s' has no valid associated object to modify."), __FUNCTION__, *RowData->GetDisplayName(SelectedRow).ToString());
				continue;
			}
		
			// Use handles if valid, otherwise use ImportText. Need to use the handles to ensure the Blueprints update properly.
			
			// Transactions are handled automatically by the handles, so no need to start a new transaction.
			if (const TWeakPtr<IPropertyHandle>* SelectedHandlePtr = RowData->PropertyNamesToHandles.Find(PropertyName))
			{
				if (SelectedHandlePtr->IsValid())
				{				
					SelectedHandlePtr->Pin()->SetValueFromFormattedString(ValueAsString, Flags);
					continue;
				}
			}

			// Handles approach failed, so use ImportText
			if (FProperty* PropertyToChange = FindFProperty<FProperty>(ObjectToModify->GetClass(), PropertyName))
			{
				if (void* ValuePtr = PropertyToChange->ContainerPtrToValuePtr<void>(ObjectToModify))
				{
					if (bShouldTransact)
					{
						ObjectToModify->Modify();
					}

					// Set the actual property value
					EPropertyChangeType::Type ChangeType =
						InFlags == EPropertyValueSetFlags::InteractiveChange
							? EPropertyChangeType::Interactive
							: EPropertyChangeType::ValueSet;

					// Set the actual property value
					PropertyToChange->ImportText_Direct(*ValueAsString, ValuePtr, ObjectToModify, PPF_None);
					FPropertyChangedEvent ChangeEvent(
						PropertyToChange,
						ChangeType,
						MakeArrayView({ObjectToModify}));
					ObjectToModify->PostEditChangeProperty(ChangeEvent);

					// Propagate to outers
					UObject* Outer = ObjectToModify->GetOuter();
					while (Outer) 
					{
						FPropertyChangedEvent ActorChangeEvent(
							PropertyToChange,
							ChangeType,
							MakeArrayView({Outer}));
						Outer->PostEditChangeProperty(ActorChangeEvent);

						Outer = Outer->GetOuter();
					}
				}
			}
		}
		
		if (bShouldTransact)
		{
			GEditor->EndTransaction();
		}
	}
}

void FObjectMixerEditorListRowData::PropagateChangesToSimilarSelectedRowProperties(
	const TSharedRef<ISceneOutlinerTreeItem> InRow, const FPropertyPropagationInfo PropertyPropagationInfo)
{
	if (PropertyPropagationInfo.PropertyName == NAME_None)
	{
		return;
	}

	if (!GetIsSelected(InRow))
	{
		return;
	}

	FObjectMixerEditorListRowData* RowData = FObjectMixerUtils::GetRowData(InRow);
	
	const TWeakPtr<IPropertyHandle>* HandlePtr = RowData->PropertyNamesToHandles.Find(PropertyPropagationInfo.PropertyName);
	if (HandlePtr && HandlePtr->IsValid())
	{
		const TArray<TSharedPtr<ISceneOutlinerTreeItem>> OtherSelectedItems = RowData->GetSelectedTreeViewItems();
		if (OtherSelectedItems.Num())
		{
			FString ValueAsString;
			(*HandlePtr).Pin()->GetValueAsFormattedString(ValueAsString);
		
			SetValueOnSelectedItems(
				ValueAsString, OtherSelectedItems, PropertyPropagationInfo.PropertyName,
				InRow, PropertyPropagationInfo.PropertyValueSetFlags);
		}
	}
}

const FObjectMixerEditorListRowData::FTransientEditorVisibilityRules& FObjectMixerEditorListRowData::GetVisibilityRules() const
{
	return VisibilityRules;
}

void FObjectMixerEditorListRowData::SetVisibilityRules(const FTransientEditorVisibilityRules& InVisibilityRules)
{
	VisibilityRules = InVisibilityRules;
}
