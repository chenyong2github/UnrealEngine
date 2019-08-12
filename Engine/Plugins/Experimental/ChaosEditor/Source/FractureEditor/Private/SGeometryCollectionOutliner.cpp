// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SGeometryCollectionOutliner.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"

#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "FractureEditorMode.h"

#define LOCTEXT_NAMESPACE "ChaosEditor"


void SGeometryCollectionOutliner::Construct(const FArguments& InArgs)
{
	BoneSelectionChangedDelegate = InArgs._OnBoneSelectionChanged;
	bPerformingSelection = false;

	ChildSlot
	[
		SAssignNew(TreeView, STreeView<FGeometryCollectionTreeItemPtr>)
		.TreeItemsSource(reinterpret_cast<FGeometryCollectionTreeItemList*>(&RootNodes))
		.OnSelectionChanged(this, &SGeometryCollectionOutliner::OnSelectionChanged)
		.OnGenerateRow(this, &SGeometryCollectionOutliner::MakeTreeRowWidget)
		.OnGetChildren(this, &SGeometryCollectionOutliner::OnGetChildren)
		.AllowInvisibleItemSelection(true)
		.OnSetExpansionRecursive(this, &SGeometryCollectionOutliner::ExpandRecursive)
	];
}

TSharedRef<ITableRow> SGeometryCollectionOutliner::MakeTreeRowWidget(FGeometryCollectionTreeItemPtr InItem, const TSharedRef<STableViewBase>& InOwnerTable)
{
	return InItem->MakeTreeRowWidget(InOwnerTable);
}

void SGeometryCollectionOutliner::OnGetChildren(FGeometryCollectionTreeItemPtr InItem, TArray<FGeometryCollectionTreeItemPtr>& OutChildren)
{
	InItem->GetChildren(OutChildren);
}

void SGeometryCollectionOutliner::UpdateGeometryCollection()
{
	/*if (TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> CollectionPtr = GeometryCollection.Pin())
	{
		RootNodes.Empty();
		GuidIndexMap.Empty();

		int NumElements = CollectionPtr->NumElements(FGeometryCollection::TransformGroup);

		const TManagedArray<FGuid>& Guids = CollectionPtr->GetAttribute<FGuid>("GUID", "Transform");
		const TManagedArray<int32>& Parents = CollectionPtr->Parent;

		// Make a copy of the old guids list ... probably a faster way to do this...
		TArray<FGuid> OldGuids;
		NodesMap.GenerateKeyArray(OldGuids);

		// Add a sub item to the outliner tree for each of the bones/chunks in this GeometryCollection
		for (int Index = 0; Index < NumElements; Index++)
		{

			ItemPtr Item;
			int32 FoundOldGuidIndex;
			if (OldGuids.Find(Guids[Index], FoundOldGuidIndex))
			{
				// Mark that this item is reused
				OldGuids.RemoveAt(FoundOldGuidIndex);
				Item = NodesMap[Guids[Index]];
			}
			else
			{
				// this is a new bone item, make a new tree item
				Item = MakeShared<FGeometryCollectionTreeItem>(Guids[Index]);
				NodesMap.Add(Guids[Index], Item);
			}

			if (Parents[Index] == FGeometryCollection::Invalid)
			{
				RootNodes.Push(Item);
			}

			GuidIndexMap.Add(Guids[Index], Index);
		}

		// Remove unused Guids
		for (auto UnusedGuid : OldGuids)
		{
			NodesMap.Remove(UnusedGuid);
		}

	}
	else
	{
		RootNodes.Empty();
		GuidIndexMap.Empty();
		NodesMap.Empty();	
	}*/
	TreeView->RequestTreeRefresh();
	ExpandAll();
}

void SGeometryCollectionOutliner::SetComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents)
{
	RootNodes.Empty();

	for (UGeometryCollectionComponent* Component : InNewComponents)
	{
		RootNodes.Add(MakeShared<FGeometryCollectionTreeItemComponent>(Component));
		TArray<int32> SelectedBones = Component->GetSelectedBones();
		SetBoneSelection(Component, SelectedBones, !SelectedBones.Num());
	}

	TreeView->RequestTreeRefresh();
	ExpandAll();
}

void SGeometryCollectionOutliner::ExpandAll()
{
	for (TSharedPtr<FGeometryCollectionTreeItemComponent> ItemPtr : RootNodes)
	{
		ItemPtr->ExpandAll(TreeView);
	}
}

void SGeometryCollectionOutliner::ExpandRecursive(TSharedPtr<FGeometryCollectionTreeItem> ItemPtr, bool bInExpansionState) const
{
	TreeView->SetItemExpansion(ItemPtr, bInExpansionState);

	FGeometryCollectionTreeItemList ItemChildren;
	ItemPtr->GetChildren(ItemChildren);
	for (auto& Child : ItemChildren)
	{
		ExpandRecursive(Child, bInExpansionState);
	}
}

void SGeometryCollectionOutliner::SetBoneSelection(UGeometryCollectionComponent* RootComponent, const TArray<int32>& InSelection, bool bClearCurrentSelection)
{
	TGuardValue<bool> ExternalSelectionGuard(bPerformingSelection, true);

	if (bClearCurrentSelection)
	{
		TreeView->ClearSelection();
	}

	bool bFirstSelection = true;

	for(auto& RootNode : RootNodes)
	{
		if (RootNode->GetComponent() == RootComponent)
		{
			for(int32 BoneIndex : InSelection)
			{
				FGeometryCollectionTreeItemPtr Item = RootNode->GetItemFromBoneIndex(BoneIndex);
				if (ensure(Item.IsValid()))
				{
					if (bFirstSelection)
					{
						TreeView->RequestScrollIntoView(Item);
						bFirstSelection = false;
					}
					TreeView->SetItemSelection(Item, true);
				}
			}
			break;
		}
	}
}

void SGeometryCollectionOutliner::OnSelectionChanged(FGeometryCollectionTreeItemPtr Item, ESelectInfo::Type SelectInfo)
{
	if(!bPerformingSelection && BoneSelectionChangedDelegate.IsBound())
	{
		TMap<UGeometryCollectionComponent*, TArray<int32>> ComponentToBoneSelectionMap;

		ComponentToBoneSelectionMap.Reserve(RootNodes.Num());

		// Create an entry for each component in the tree.  If the component has no selected bones then we return an empty array to signal that the selection should be cleared
		for (auto& Root : RootNodes)
		{
			ComponentToBoneSelectionMap.Add(Root->GetComponent(), TArray<int32>());
		}

		FGeometryCollectionTreeItemList SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);
		
		FScopedTransaction Transaction(FractureTransactionContexts::SelectBoneContext, LOCTEXT("SelectGeometryCollectionBoneTransaction", "Select Bone"), Item->GetComponent());
		for(auto& SelectedItem : SelectedItems)
		{
			if (SelectedItem->GetBoneIndex() != INDEX_NONE)
			{
				TArray<int32>& SelectedBones = ComponentToBoneSelectionMap.FindChecked(Item->GetComponent());
				SelectedBones.Add(SelectedItem->GetBoneIndex());
				Item->GetComponent()->Modify();
			}
		}
		// Fire off the delegate for each component
		for (auto& SelectionPair : ComponentToBoneSelectionMap)
		{
			BoneSelectionChangedDelegate.Execute(SelectionPair.Key, SelectionPair.Value);
		}
	}
}

TSharedRef<ITableRow> FGeometryCollectionTreeItemComponent::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	FString ActorName = Component->GetOwner()->GetActorLabel();
	FString ComponentName = Component->GetClass()->GetFName().ToString();

	return SNew(STableRow<FGeometryCollectionTreeItemPtr>, InOwnerTable)
		.Content()
		[
			SNew(STextBlock).Text(FText::FromString(ActorName + '.' + ComponentName))
		];
}

void FGeometryCollectionTreeItemComponent::GetChildren(FGeometryCollectionTreeItemList& OutChildren)
{
	OutChildren = MyChildren;
}

FGeometryCollectionTreeItemPtr FGeometryCollectionTreeItemComponent::GetItemFromBoneIndex(int32 BoneIndex) const
{
	for (auto& Pair : NodesMap)
	{
		if (Pair.Value->GetBoneIndex() == BoneIndex)
		{
			return Pair.Value;
		}
	}

	return FGeometryCollectionTreeItemPtr();
}

void FGeometryCollectionTreeItemComponent::GetChildrenForBone(FGeometryCollectionTreeItemBone& BoneItem, FGeometryCollectionTreeItemList& OutChildren) const
{
	if(const UGeometryCollection* RestCollection = Component->GetRestCollection())
	{
		if (FGeometryCollection* Collection = RestCollection->GetGeometryCollection().Get())
		{
			if (const int32* BoneIndex = GuidIndexMap.Find(BoneItem.GetGuid()))
			{
				const TManagedArray<TSet<int32>>& Children = Collection->Children;
				const TManagedArray<FGuid>& Guids = Collection->GetAttribute<FGuid>("GUID", "Transform");
				for (auto ChildIndex : Children[*BoneIndex])
				{
					FGuid ChildGuid = Guids[ChildIndex];
					FGeometryCollectionTreeItemPtr ChildPtr = NodesMap.FindRef(ChildGuid);
					if (ChildPtr.IsValid())
					{
						OutChildren.Add(ChildPtr);
					}
				}
			}
		}
	}
}

FText FGeometryCollectionTreeItemComponent::GetDisplayNameForBone(const FGuid& Guid) const
{
	if(FGeometryCollection* Collection = Component->GetRestCollection()->GetGeometryCollection().Get())
	{
		const TManagedArray<FString>& BoneNames = Collection->BoneName;
		
		if(const int32* BoneIndex = GuidIndexMap.Find(Guid))
		{
			if(*BoneIndex < BoneNames.Num())
			{
				return FText::FromString(BoneNames[*BoneIndex]);
			}
			else
			{
				return FText::Format(LOCTEXT("BoneNameNotFound", "Bone Name Not Found: Index {0}"), (*BoneIndex));
			}
		}
	}

	return LOCTEXT("BoneNotFound", "Bone Not Found, Invalid Geometry Collection");
}

void FGeometryCollectionTreeItemComponent::ExpandAll(TSharedPtr<STreeView<FGeometryCollectionTreeItemPtr>> TreeView)
{
	TreeView->SetItemExpansion(AsShared(), true);

	for (auto& Elem : NodesMap)
	{
	    TreeView->SetItemExpansion(Elem.Value, true);
	}
}

void FGeometryCollectionTreeItemComponent::RegenerateChildren()
{
	if(Component->GetRestCollection())
	{
		//@todo Fracture:  This is potentially very expensive to refresh with giant trees
		FGeometryCollection* Collection = Component->GetRestCollection()->GetGeometryCollection().Get();

		NodesMap.Empty();
		GuidIndexMap.Empty();
		MyChildren.Empty();

		if (Collection)
		{
			int32 NumElements = Collection->NumElements(FGeometryCollection::TransformGroup);

			const TManagedArray<FGuid>& Guids = Collection->GetAttribute<FGuid>("GUID", "Transform");
			const TManagedArray<int32>& Parents = Collection->Parent;
			// const TManagedArray<FString>& BoneNames = CollectionPtr->BoneName;

			RootIndex = FGeometryCollection::Invalid;

			// Add a sub item to the outliner tree for each of the bones/chunks in this GeometryCollection
			for (int32 Index = 0; Index < NumElements; Index++)
			{
				TSharedRef<FGeometryCollectionTreeItemBone> NewItem = MakeShared<FGeometryCollectionTreeItemBone>(Guids[Index], Index, *this);
				if (Parents[Index] == RootIndex)
				{
					// The actual children directly beneath this node are the ones without a parent.  The rest are children of children
					MyChildren.Add(NewItem);
				}

				NodesMap.Add(Guids[Index], NewItem);
				GuidIndexMap.Add(Guids[Index], Index);
			}
		}
	}
}

TSharedRef<ITableRow> FGeometryCollectionTreeItemBone::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(STableRow<FGeometryCollectionTreeItemPtr>, InOwnerTable)
		.Content()
		[
			SNew(STextBlock).Text(ParentComponentItem->GetDisplayNameForBone(Guid))
		];
}

void FGeometryCollectionTreeItemBone::GetChildren(FGeometryCollectionTreeItemList& OutChildren)
{
	ParentComponentItem->GetChildrenForBone(*this, OutChildren);
}

#undef LOCTEXT_NAMESPACE // "ChaosEditor"