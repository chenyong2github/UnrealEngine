// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerUtils.h"

#include "ObjectMixerEditorSerializedData.h"
#include "Views/List/RowTypes/ObjectMixerEditorListRowActor.h"
#include "Views/List/RowTypes/ObjectMixerEditorListRowComponent.h"
#include "Views/List/RowTypes/ObjectMixerEditorListRowFolder.h"
#include "Views/List/RowTypes/ObjectMixerEditorListRowUObject.h"
#include "Views/List/SObjectMixerEditorList.h"

FObjectMixerEditorListRowFolder* FObjectMixerUtils::AsFolderRow(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{
	// Sometimes the outliner reports type as the parent type, but we can be assured we're always using our type.
	// For this reason, we check for our type first and if it's not our type, check parent type then cast.
	if (FObjectMixerEditorListRowFolder* AsFolder = InTreeItem->CastTo<FObjectMixerEditorListRowFolder>())
	{
		return AsFolder;
	}
	
	if (FActorFolderTreeItem* AsFolder = InTreeItem->CastTo<FActorFolderTreeItem>())
	{
		if (AsFolder->GetFolder().IsValid())
		{
			return StaticCast<FObjectMixerEditorListRowFolder*>(AsFolder);
		}
	}

	return nullptr;
}

FObjectMixerEditorListRowActor* FObjectMixerUtils::AsActorRow(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{
	// Sometimes the outliner reports type as the parent type, but we can be assured we're always using our type.
	// For this reason, we check for our type first and if it's not our type, check parent type then cast.
	if (FObjectMixerEditorListRowActor* AsActor = InTreeItem->CastTo<FObjectMixerEditorListRowActor>())
	{
		return AsActor;
	}
	
	if (FActorTreeItem* AsActor = InTreeItem->CastTo<FActorTreeItem>())
	{
		if (AsActor->Actor.IsValid())
		{
			return StaticCast<FObjectMixerEditorListRowActor*>(AsActor);
		}
	}

	return nullptr;
}

FObjectMixerEditorListRowComponent* FObjectMixerUtils::AsComponentRow(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{
	// Sometimes the outliner reports type as the parent type, but we can be assured we're always using our type.
	// For this reason, we check for our type first and if it's not our type, check parent type then cast.
	if (FObjectMixerEditorListRowComponent* AsComponent = InTreeItem->CastTo<FObjectMixerEditorListRowComponent>())
	{
		return AsComponent;
	}
	
	if (FComponentTreeItem* AsComponent = InTreeItem->CastTo<FComponentTreeItem>())
	{
		if (AsComponent->Component.IsValid())
		{
			return StaticCast<FObjectMixerEditorListRowComponent*>(AsComponent);
		}
	}

	return nullptr;
}

FObjectMixerEditorListRowUObject* FObjectMixerUtils::AsObjectRow(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{
	if (FObjectMixerEditorListRowUObject* AsObject = InTreeItem->CastTo<FObjectMixerEditorListRowUObject>())
	{
		if (IsValid(AsObject->ObjectPtr))
		{
			return AsObject;
		}
	}

	return nullptr;
}

FObjectMixerEditorListRowData* FObjectMixerUtils::GetRowData(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{
	if (FObjectMixerEditorListRowFolder* AsFolder = AsFolderRow(InTreeItem))
	{
		return &AsFolder->RowData;
	}

	if (FObjectMixerEditorListRowActor* AsActor = AsActorRow(InTreeItem))
	{
		return &AsActor->RowData;
	}

	if (FObjectMixerEditorListRowComponent* AsComponent = AsComponentRow(InTreeItem))
	{
		return &AsComponent->RowData;
	}

	if (FObjectMixerEditorListRowUObject* AsObject = AsObjectRow(InTreeItem))
	{
		return &AsObject->RowData;
	}

	return nullptr;
}

UObject* FObjectMixerUtils::GetRowObject(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem, const bool bGetHybridRowComponent)
{	
	if (const FObjectMixerEditorListRowActor* ActorRow = AsActorRow(InTreeItem))
	{
		if (bGetHybridRowComponent && ActorRow->RowData.GetIsHybridRow())
		{
			return ActorRow->RowData.GetHybridComponent();
		}
		
		return ActorRow->Actor.Get();
	}

	if (const FObjectMixerEditorListRowComponent* ComponentRow = AsComponentRow(InTreeItem))
	{
		return ComponentRow->Component.Get();
	}

	if (const FObjectMixerEditorListRowUObject* ObjectRow = AsObjectRow(InTreeItem))
	{
		return ObjectRow->ObjectPtr.Get();
	}

	return nullptr;
}

AActor* FObjectMixerUtils::GetSelfOrOuterAsActor(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{
	if (UObject* Object = GetRowObject(InTreeItem))
	{
		AActor* Actor = Cast<AActor>(Object);

		if (!Actor)
		{
			Actor = Object->GetTypedOuter<AActor>();
		}

		return Actor;
	}

	return nullptr;
}

bool FObjectMixerUtils::IsObjectRefInCollection(const FName& CollectionName, const UObject* Object, const TSharedPtr<FObjectMixerEditorList> ListModel)
{	
	if (Object)
	{
		if (CollectionName == UObjectMixerEditorSerializedData::AllCollectionName)
		{
			return true;
		}

		return ListModel->IsObjectInCollection(CollectionName, Object);
	}
	
	return false;
}

bool FObjectMixerUtils::IsObjectRefInCollection(const FName& CollectionName, TSharedPtr<ISceneOutlinerTreeItem> InTreeItem)
{	
	if (const UObject* Object = GetRowObject(InTreeItem))
	{
		if (SObjectMixerEditorList* ListView = GetRowData(InTreeItem)->GetListView())
		{
			if (const TSharedPtr<FObjectMixerEditorList> ListModel = ListView->GetListModelPtr().Pin())
			{
				return IsObjectRefInCollection(CollectionName, Object, ListModel);
			}
		}
	}
	
	return false;
}

void FObjectMixerUtils::SetChildRowsSelected(
	TSharedPtr<ISceneOutlinerTreeItem> InTreeItem, const bool bNewSelected, const bool bRecursive)
{
	for (const TWeakPtr<ISceneOutlinerTreeItem>& ChildRow : InTreeItem->GetChildren())
	{
		if (const TSharedPtr<ISceneOutlinerTreeItem> PinnedChildRow = ChildRow.Pin())
		{
			// Recurse even if not visible
			if (bRecursive)
			{
				SetChildRowsSelected(PinnedChildRow, bNewSelected, bRecursive);
			}
	
			GetRowData(PinnedChildRow)->SetIsSelected(PinnedChildRow.ToSharedRef(), bNewSelected);
		}
	}
}
