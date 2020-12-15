// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlHierarchy.h"
#include "Widgets/Input/SComboButton.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SSearchBox.h"
#include "ControlRigEditMode.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ControlRig.h"
#include "ControlRigEditorStyle.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SControlHierarchy"

//////////////////////////////////////////////////////////////
/// FControlTreeElement
///////////////////////////////////////////////////////////
FControlTreeElement::FControlTreeElement(const FRigElementKey& InKey, TWeakPtr<SControlHierarchy> InHierarchyHandler)
{
	Key = InKey;
}

TSharedRef<ITableRow> FControlTreeElement::MakeTreeRowWidget( const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FControlTreeElement> InRigTreeElement, TSharedPtr<SControlHierarchy> InHierarchy)
{
	return SNew(SControlHierarchyItem, InOwnerTable, InRigTreeElement, InHierarchy);
		
}


//////////////////////////////////////////////////////////////
/// SControlHierarchyItem
///////////////////////////////////////////////////////////
void SControlHierarchyItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FControlTreeElement> InRigTreeElement, TSharedPtr<SControlHierarchy> InHierarchy)
{
	WeakRigTreeElement = InRigTreeElement;

	TSharedPtr< SInlineEditableTextBlock > InlineWidget;

	const FSlateBrush* Brush = nullptr;
	switch (InRigTreeElement->Key.Type)
		{
		case ERigElementType::Control:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.Control");
			break;
		}
	
		default:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.BoneUser");
			break;
		}
	}

	STableRow<TSharedPtr<FControlTreeElement>>::Construct(
		STableRow<TSharedPtr<FControlTreeElement>>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.MaxWidth(18)
		.FillWidth(1.0)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(Brush)
		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(InlineWidget, SInlineEditableTextBlock)
			.Text(this, &SControlHierarchyItem::GetName)
		.MultiLine(false)
		]
		], OwnerTable);

}

FText SControlHierarchyItem::GetName() const
{
	return (FText::FromName(WeakRigTreeElement.Pin()->Key.Name));
}

///////////////////////////////////////////////////////////

SControlHierarchy::~SControlHierarchy()
{
	if (ControlRig.IsValid())
	{
		ControlRig->ControlSelected().RemoveAll(this);
	}	
}

void SControlHierarchy::Construct(const FArguments& InArgs, UControlRig* InControlRig)
{
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(3.0f, 1.0f)
					[
						SAssignNew(FilterBox, SSearchBox)
						.OnTextChanged(this, &SControlHierarchy::OnFilterTextChanged)
					]
				]
			]
		]
		+SVerticalBox::Slot()
		.Padding(0.0f, 0.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FEditorStyle::GetBrush("SCSEditor.TreePanel"))
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FControlTreeElement>>)
				.TreeItemsSource(&RootElements)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SControlHierarchy::MakeTableRowWidget)
				.OnGetChildren(this, &SControlHierarchy::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SControlHierarchy::OnSelectionChanged)
				.HighlightParentNodesForSelection(true)
				.ItemHeight(24)
			]
		]
	];

	SetControlRig(InControlRig);
}

UControlRig* SControlHierarchy::GetControlRig() const
{
	return ControlRig.Get();
}

void SControlHierarchy::SetControlRig(UControlRig* InControlRig)
{
	bSelecting = false;
	if (ControlRig.IsValid())
	{
		ControlRig->ControlSelected().RemoveAll(this);
	}
	ControlRig = InControlRig;
	if (ControlRig.IsValid())
	{
		ControlRig->ControlSelected().AddRaw(this, &SControlHierarchy::OnRigElementSelected);
	}
	RefreshTreeView();

}
void SControlHierarchy::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;

	RefreshTreeView();
}

void SControlHierarchy::RefreshTreeView()
{
	
	TMap<FRigElementKey, bool> ExpansionState;
	for (TPair<FRigElementKey, TSharedPtr<FControlTreeElement>> Pair : ElementMap)
	{
		ExpansionState.FindOrAdd(Pair.Key) = TreeView->IsItemExpanded(Pair.Value);
	}

	RootElements.Reset();
	ElementMap.Reset();
	ParentMap.Reset();

	if (ControlRig.IsValid())
	{
		TArray<FRigControl> SortedControls;
		ControlRig->GetControlsInOrder(SortedControls);
		for (const FRigControl& Element : SortedControls)
		{
			if (!ControlRig->IsCurveControl(&Element))
			{
				AddControlElement(Element);
			}
		}
		if (ExpansionState.Num() == 0)
		{
			for (TSharedPtr<FControlTreeElement> RootElement : RootElements)
			{
				SetExpansionRecursive(RootElement, false);
			}
		}
		else
		{
			for (TPair<FRigElementKey, bool> Pair : ExpansionState)
			{
				if (!Pair.Value)
				{
					continue;
				}
				if (TSharedPtr<FControlTreeElement>* ItemPtr = ElementMap.Find(Pair.Key))
				{
					TreeView->SetItemExpansion(*ItemPtr, true);
				}
			}
		}

		TreeView->RequestTreeRefresh();

		FRigControlHierarchy& ControlHierarchy = ControlRig->GetControlHierarchy();
		TArray<FName> Selection = ControlHierarchy.CurrentSelection();
		for (const FName& Name : Selection)
		{
			for (const FRigControl& Control : ControlHierarchy.GetControls())
			{
				if (Name == Control.Name)
				{
					OnRigElementSelected(ControlRig.Get(),Control, true);
					break;
				}
			}
		}
	}
	else
	{
		TreeView->RequestTreeRefresh();
	}
}

void SControlHierarchy::SetExpansionRecursive(TSharedPtr<FControlTreeElement> InElement, bool bTowardsParent)
{
	TreeView->SetItemExpansion(InElement, true);

	if (bTowardsParent)
	{
		if (const FRigElementKey* ParentKey = ParentMap.Find(InElement->Key))
		{
			if (TSharedPtr<FControlTreeElement>* ParentItem = ElementMap.Find(*ParentKey))
			{
				SetExpansionRecursive(*ParentItem, bTowardsParent);
			}
		}
	}
	else
	{
		for (int32 ChildIndex = 0; ChildIndex < InElement->Children.Num(); ++ChildIndex)
		{
			SetExpansionRecursive(InElement->Children[ChildIndex], bTowardsParent);
		}
	}
}
TSharedRef<ITableRow> SControlHierarchy::MakeTableRowWidget(TSharedPtr<FControlTreeElement> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget( OwnerTable, InItem.ToSharedRef(), SharedThis(this));
}

void SControlHierarchy::HandleGetChildrenForTree(TSharedPtr<FControlTreeElement> InItem, TArray<TSharedPtr<FControlTreeElement>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SControlHierarchy::OnSelectionChanged(TSharedPtr<FControlTreeElement> Selection, ESelectInfo::Type SelectInfo)
{
	if (!bSelecting)
	{
		TGuardValue<bool> Guard(bSelecting, true);
		FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();

		if (Hierarchy)
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), !GIsTransacting);

			TArray<FRigElementKey> OldSelection = Hierarchy->CurrentSelection();
			TArray<FRigElementKey> NewSelection;

			TArray<TSharedPtr<FControlTreeElement>> SelectedItems = TreeView->GetSelectedItems();
			for (const TSharedPtr<FControlTreeElement>& SelectedItem : SelectedItems)
			{
				NewSelection.Add(SelectedItem->Key);
			}

			for (const FRigElementKey& PreviouslySelected : OldSelection)
			{
				if (NewSelection.Contains(PreviouslySelected))
				{
					continue;
				}
				Hierarchy->Select(PreviouslySelected, false);
			}

			for (const FRigElementKey& NewlySelected : NewSelection)
			{
				Hierarchy->Select(NewlySelected, true);
			}
		}
	}
}


TSharedPtr<FControlTreeElement> SControlHierarchy::FindElement(const FRigElementKey& InElementKey, TSharedPtr<FControlTreeElement> CurrentItem)
{
	if (CurrentItem->Key == InElementKey)
	{
		return CurrentItem;
	}

	for (int32 ChildIndex = 0; ChildIndex < CurrentItem->Children.Num(); ++ChildIndex)
	{
		TSharedPtr<FControlTreeElement> Found = FindElement(InElementKey, CurrentItem->Children[ChildIndex]);
		if (Found.IsValid())
		{
			return Found;
		}
	}

	return TSharedPtr<FControlTreeElement>();
}

void SControlHierarchy::OnRigElementSelected(UControlRig* Subject, const FRigControl& Control, bool bSelected)
{
	
	FRigElementKey Key;
	Key.Name = Control.Name;
	Key.Type = ERigElementType::Control;
	for (int32 RootIndex = 0; RootIndex < RootElements.Num(); ++RootIndex)
	{
		TSharedPtr<FControlTreeElement> Found = FindElement(Key, RootElements[RootIndex]);
		if (Found.IsValid())
		{
			TreeView->SetItemSelection(Found, bSelected, ESelectInfo::OnNavigation);
			TArray<TSharedPtr<FControlTreeElement>> SelectedItems = TreeView->GetSelectedItems();
			for (TSharedPtr<FControlTreeElement> SelectedItem : SelectedItems)
			{
				SetExpansionRecursive(SelectedItem, true);
			}

			if (SelectedItems.Num() > 0)
			{
				TreeView->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}
	
}

void SControlHierarchy::AddControlElement(FRigControl InControl)
{
	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	FRigElementKey ParentKey;
	if (InControl.SpaceIndex != INDEX_NONE)
	{
		AddSpaceElement(SpaceHierarchy[InControl.SpaceIndex]);
		ParentKey = SpaceHierarchy[InControl.SpaceIndex].GetElementKey();
	}
	else if (InControl.ParentIndex != INDEX_NONE)
	{
		AddControlElement(ControlHierarchy[InControl.ParentIndex]);
		ParentKey = ControlHierarchy[InControl.ParentIndex].GetElementKey();
	}
	AddElement(InControl.GetElementKey(), ParentKey);
}


void SControlHierarchy::AddSpaceElement(FRigSpace InSpace)
{
	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	FRigElementKey ParentKey;
	if (InSpace.ParentIndex != INDEX_NONE)
	{
		switch (InSpace.SpaceType)
		{
		case ERigSpaceType::Control:
		{
			AddControlElement(ControlHierarchy[InSpace.ParentIndex]);
			ParentKey = ControlHierarchy[InSpace.ParentIndex].GetElementKey();
			break;
		}
		case ERigSpaceType::Space:
		{
			AddSpaceElement(SpaceHierarchy[InSpace.ParentIndex]);
			ParentKey = SpaceHierarchy[InSpace.ParentIndex].GetElementKey();
			break;
		}
		default:
		{
			break;
		}
		}
	}
	AddElement(InSpace.GetElementKey(), ParentKey);
}

void SControlHierarchy::AddElement(FRigElementKey InKey, FRigElementKey InParentKey)
{
	if (ElementMap.Contains(InKey))
	{
		return;
	}

	FString FilteredString = FilterText.ToString();
	if (FilteredString.IsEmpty())
	{
		TSharedPtr<FControlTreeElement> NewItem = MakeShared<FControlTreeElement>(InKey, SharedThis(this));
		ElementMap.Add(InKey, NewItem);
		if (InParentKey)
		{
			ParentMap.Add(InKey, InParentKey);
		}

		if (InParentKey)
		{
			TSharedPtr<FControlTreeElement>* FoundItem = ElementMap.Find(InParentKey);
			check(FoundItem);
			FoundItem->Get()->Children.Add(NewItem);
		}
		else
		{
			RootElements.Add(NewItem);
		}
	}
	else
	{
		FString FilteredStringUnderScores = FilteredString.Replace(TEXT(" "), TEXT("_"));
		if (InKey.Name.ToString().Contains(FilteredString) || InKey.Name.ToString().Contains(FilteredStringUnderScores))
		{
			bool bExists = false;
			for (int32 RootIndex = 0; RootIndex < RootElements.Num(); ++RootIndex)
			{
				TSharedPtr<FControlTreeElement> Found = FindElement(InKey, RootElements[RootIndex]);
				if (Found.IsValid())
				{
					bExists = true;
					break;
				}
			}

			if (!bExists)
			{
				TSharedPtr<FControlTreeElement> NewItem = MakeShared<FControlTreeElement>(InKey, SharedThis(this));
				RootElements.Add(NewItem);
			}
		}
	}
}


FRigHierarchyContainer* SControlHierarchy::GetHierarchyContainer() const
{
	if (ControlRig.IsValid())
	{
		return ControlRig->GetHierarchy();
	}

	return nullptr;
}
#undef LOCTEXT_NAMESPACE