// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGeometryCollectionOutliner.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"

#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "FractureEditorMode.h"
#include "ScopedTransaction.h"
#include "FractureSettings.h"
#include "GeometryCollectionOutlinerDragDrop.h"

#define LOCTEXT_NAMESPACE "ChaosEditor"

void FGeometryCollectionTreeItem::OnDragLeave(const FDragDropEvent& InDragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = InDragDropEvent.GetOperation();
	if (Operation.IsValid())
	{
		if (Operation->IsOfType<FGeometryCollectionBoneDragDrop>())
		{
			TSharedPtr<FGeometryCollectionBoneDragDrop> GeometryCollectionBoneOp = InDragDropEvent.GetOperationAs<FGeometryCollectionBoneDragDrop>();
			const FSlateBrush* Icon = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
			GeometryCollectionBoneOp->SetToolTip(FText(), Icon);
		}
	}
}

void FGeometryCollectionTreeItemBone::OnDragEnter(FDragDropEvent const& InDragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = InDragDropEvent.GetOperation();
	if (Operation.IsValid())
	{
		if (Operation->IsOfType<FGeometryCollectionBoneDragDrop>())
		{
			TSharedPtr<FGeometryCollectionBoneDragDrop> GeometryCollectionBoneOp = InDragDropEvent.GetOperationAs<FGeometryCollectionBoneDragDrop>();
			
			UGeometryCollectionComponent* SourceComponent = GetComponent();
			FGeometryCollectionEdit GeometryCollectionEdit = SourceComponent->EditRestCollection();
			if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
			{
				FGeometryCollection* GeometryCollection = GeometryCollectionObject->GetGeometryCollection().Get();
				FText HoverText;
				bool bValid = GeometryCollectionBoneOp->ValidateDrop(GeometryCollection, BoneIndex, HoverText);
				const FSlateBrush* Icon = bValid ? FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK")) : FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				GeometryCollectionBoneOp->SetToolTip(HoverText, Icon);
			}
		}
	}
}

FReply FGeometryCollectionTreeItemBone::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		UGeometryCollectionComponent* SourceComponent = GetComponent();

		FGeometryCollectionEdit GeometryCollectionEdit = SourceComponent->EditRestCollection();
		if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection,ESPMode::ThreadSafe> GeometryCollection = GeometryCollectionObject->GetGeometryCollection();
			TArray<int32> SelectedBones = SourceComponent->GetSelectedBones();

			return FReply::Handled().BeginDragDrop(FGeometryCollectionBoneDragDrop::New(GeometryCollection, SelectedBones));
		}		
	}

	return FReply::Unhandled();
}

FReply FGeometryCollectionTreeItemBone::OnDrop(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return FReply::Unhandled();
	}
	
	if (Operation->IsOfType<FGeometryCollectionBoneDragDrop>())
	{
		TSharedPtr<FGeometryCollectionBoneDragDrop> GeometryCollectionBoneDragDrop = StaticCastSharedPtr<FGeometryCollectionBoneDragDrop>(Operation);

		UGeometryCollectionComponent* SourceComponent = GetComponent();

		FGeometryCollectionEdit GeometryCollectionEdit = SourceComponent->EditRestCollection();
		if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
		{
			FGeometryCollection* GeometryCollection = GeometryCollectionObject->GetGeometryCollection().Get();
			if (GeometryCollectionBoneDragDrop->ReparentBones(GeometryCollection, BoneIndex))
			{
				ParentComponentItem->RegenerateChildren();
				ParentComponentItem->RequestTreeRefresh();
				ParentComponentItem->ExpandAll();
			}
		}		
	}
	
	return FReply::Unhandled();
}


UOutlinerSettings::UOutlinerSettings(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, ItemText(EOutlinerItemNameEnum::BoneIndex)
{}


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

void SGeometryCollectionOutliner::RegenerateItems()
{
	TreeView->RebuildList();
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
	TreeView->RequestTreeRefresh();
	ExpandAll();
}

void SGeometryCollectionOutliner::SetComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents)
{
	// Clear the cached Tree ItemSelection without affecting the SelectedBones as 
	// we want to refresh the tree selection using selected bones
	TGuardValue<bool> ExternalSelectionGuard(bPerformingSelection, true);
	TreeView->ClearSelection();

	RootNodes.Empty();

	for (UGeometryCollectionComponent* Component : InNewComponents)
	{
		if (Component->GetRestCollection() && !Component->GetRestCollection()->IsPendingKill())
		{
			RootNodes.Add(MakeShared<FGeometryCollectionTreeItemComponent>(Component, TreeView));
			TArray<int32> SelectedBones = Component->GetSelectedBones();
			SetBoneSelection(Component, SelectedBones, false);
		}
	}

	TreeView->RequestTreeRefresh();
	ExpandAll();
}

void SGeometryCollectionOutliner::ExpandAll()
{
	for (TSharedPtr<FGeometryCollectionTreeItemComponent> ItemPtr : RootNodes)
	{
		ItemPtr->ExpandAll();
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

void SGeometryCollectionOutliner::SetHistogramSelection(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones)
{
	// Find the matching component
	for (TSharedPtr<FGeometryCollectionTreeItemComponent> RootNode : RootNodes)
	{
		if (RootNode->GetComponent() == RootComponent)
		{
			// Copy the histogram selection.
			RootNode->SetHistogramSelection(SelectedBones);
			RootNode->RegenerateChildren();
			TreeView->RequestTreeRefresh();
			ExpandAll();
			return;
		}
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

		if (Item == nullptr)
		{
			TreeView->ClearSelection();
		}

		FGeometryCollectionTreeItemList SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		FScopedTransaction Transaction(FractureTransactionContexts::SelectBoneContext, LOCTEXT("SelectGeometryCollectionBoneTransaction", "Select Bone"), Item != nullptr ? Item->GetComponent() : nullptr);
		for(auto& SelectedItem : SelectedItems)
		{
			if (SelectedItem->GetBoneIndex() != INDEX_NONE)
			{
				TArray<int32>& SelectedBones = ComponentToBoneSelectionMap.FindChecked(SelectedItem->GetComponent());
				SelectedBones.Add(SelectedItem->GetBoneIndex());
				SelectedItem->GetComponent()->Modify();
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
	if (!Component.IsValid())
	{
		return;
	}
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
	if (!Component.IsValid())
	{
		return LOCTEXT("BoneNotFound", "Bone Not Found, Invalid Geometry Collection");
	}
	if (const UGeometryCollection* RestCollection = Component->GetRestCollection())
	{
		if (FGeometryCollection* Collection = RestCollection->GetGeometryCollection().Get())
		{
			const TManagedArray<FString>& BoneNames = Collection->BoneName;

			if (const int32* BoneIndex = GuidIndexMap.Find(Guid))
			{
				if (*BoneIndex < BoneNames.Num())
				{
					return FText::FromString(BoneNames[*BoneIndex]);
				}
				else
				{
					return FText::Format(LOCTEXT("BoneNameNotFound", "Bone Name Not Found: Index {0}"), (*BoneIndex));
				}
			}
		}
	}
	return LOCTEXT("BoneNotFound", "Bone Not Found, Invalid Geometry Collection");
}

void FGeometryCollectionTreeItemComponent::ExpandAll()
{
	TreeView->SetItemExpansion(AsShared(), true);

	for (auto& Elem : NodesMap)
	{
	    TreeView->SetItemExpansion(Elem.Value, true);
	}
}

void FGeometryCollectionTreeItemComponent::RegenerateChildren()
{
	if(Component.IsValid() && Component->GetRestCollection())
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
				if (FilterBoneIndex(Index))
				{
					TSharedRef<FGeometryCollectionTreeItemBone> NewItem = MakeShared<FGeometryCollectionTreeItemBone>(Guids[Index], Index, this);
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
}

void FGeometryCollectionTreeItemComponent::RequestTreeRefresh()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

void FGeometryCollectionTreeItemComponent::SetHistogramSelection(TArray<int32>& SelectedBones)
{
	HistogramSelection = SelectedBones;
}

bool FGeometryCollectionTreeItemComponent::FilterBoneIndex(int32 BoneIndex) const
{
	FGeometryCollection* Collection = Component->GetRestCollection()->GetGeometryCollection().Get();
	TManagedArray<TSet<int32>>& Children = Collection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

	if (Children[BoneIndex].Num() == 0)
	{
		// We don't display leaf nodes deeper than the view level.
		UFractureSettings* FractureSettings = GetMutableDefault<UFractureSettings>();

		if (FractureSettings->FractureLevel >= 0)
		{
			TManagedArray<int32>& Level = Collection->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);
			if (Level[BoneIndex] != FractureSettings->FractureLevel)
			{
				return false;
			}
		}

		// If anything is selected int the Histogram, we filter by that selection.
		if (HistogramSelection.Num() > 0)
		{
			if (!HistogramSelection.Contains(BoneIndex))
			{
				return false;
			}
		}		
	}

	return true;	
}

TSharedRef<ITableRow> FGeometryCollectionTreeItemBone::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	UOutlinerSettings* OutlinerSettings = GetMutableDefault<UOutlinerSettings>();
	FText ItemText = ParentComponentItem->GetDisplayNameForBone(Guid);
	if (OutlinerSettings->ItemText == EOutlinerItemNameEnum::BoneIndex)
	{
		ItemText = FText::FromString(FString::FromInt(GetBoneIndex()));
	}

	// Set color according to simulation type

	FSlateColor TextColor(FLinearColor::Red); // default color indicates something wrong

	const UGeometryCollection* RestCollection = ParentComponentItem->GetComponent()->GetRestCollection();
	if (RestCollection && !RestCollection->IsPendingKill())
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = RestCollection->GetGeometryCollection();
		const TManagedArray<int32>& SimulationType = GeometryCollectionPtr->SimulationType;
		switch (SimulationType[GetBoneIndex()])
		{
			case FGeometryCollection::ESimulationTypes::FST_None:
				TextColor = FLinearColor::Green;
				break;

			case FGeometryCollection::ESimulationTypes::FST_Rigid:
				TextColor = FLinearColor::Gray;
				break;

			case FGeometryCollection::ESimulationTypes::FST_Clustered:
				TextColor = FSlateColor(FColor::Cyan);
				break;

			default:
				ensureMsgf(false, TEXT("Invalid Geometry Collection simulation type encountered."));
				break;
		}
	}
	else
	{
		// Deleted rest collection
		TextColor = FLinearColor(0.1f, 0.1f, 0.1f);
	}
	
	return SNew(STableRow<FGeometryCollectionTreeItemPtr>, InOwnerTable)
		.Content()
		[			
			SNew(STextBlock)
			.Text(ItemText)
			.ColorAndOpacity(TextColor)
		]
		.OnDragDetected(this, &FGeometryCollectionTreeItem::OnDragDetected)
		.OnDrop(this, &FGeometryCollectionTreeItem::OnDrop)
		.OnDragEnter(this, &FGeometryCollectionTreeItem::OnDragEnter)
		.OnDragLeave(this, &FGeometryCollectionTreeItem::OnDragLeave);
}

void FGeometryCollectionTreeItemBone::GetChildren(FGeometryCollectionTreeItemList& OutChildren)
{
	ParentComponentItem->GetChildrenForBone(*this, OutChildren);
}


#undef LOCTEXT_NAMESPACE // "ChaosEditor"