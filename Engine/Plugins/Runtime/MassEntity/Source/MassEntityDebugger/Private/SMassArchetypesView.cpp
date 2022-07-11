// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMassArchetypesView.h"
#include "MassDebuggerModel.h"
#include "SMassArchetype.h"

#define LOCTEXT_NAMESPACE "SMassDebugger"


namespace UE::Mass::Debugger::UI::Private
{
	FLinearColor GetArchetypeDistanceColor(float Distance)
	{
		const FLinearColor MinDistance = FLinearColor::Green;
		const FLinearColor MaxDistance = FLinearColor::Red;
		
		return (1.f - Distance) * MinDistance + Distance * MaxDistance;
	}
}

using FMassDebuggerArchetypeDataPtr = TSharedPtr<FMassDebuggerArchetypeData, ESPMode::ThreadSafe>;

//----------------------------------------------------------------------//
// SMassArchetypeTableRowBase
//----------------------------------------------------------------------//
class SMassArchetypeTableRowBase : public STableRow<FMassDebuggerArchetypeDataPtr>
{
public:
	SLATE_BEGIN_ARGS(SMassArchetypeTableRowBase) { }
	SLATE_END_ARGS()

protected:
	void Construct(const FArguments& InArgs, const TSharedPtr<STableViewBase>& InOwnerTableView, const FMassDebuggerArchetypeDataPtr InEntryItem)
	{
		Item = InEntryItem;
		STableRow<FMassDebuggerArchetypeDataPtr>::Construct(STableRow<FMassDebuggerArchetypeDataPtr>::FArguments(), InOwnerTableView.ToSharedRef());
	}

	FMassDebuggerArchetypeDataPtr Item;
};

//----------------------------------------------------------------------//
// SMassArchetypeTableRow
//----------------------------------------------------------------------//
class SMassArchetypeTableRow : public SMassArchetypeTableRowBase
{
public:
	void Construct(const FArguments& InArgs, const TSharedPtr<STableViewBase>& InOwnerTableView, const FMassDebuggerArchetypeDataPtr InEntryItem, TSharedPtr<FMassDebuggerModel> DebuggerModel)
	{
		SMassArchetypeTableRowBase::Construct(InArgs, InOwnerTableView, InEntryItem);

		TSharedPtr<SHorizontalBox> Box = SNew(SHorizontalBox);
		Box->AddSlot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
				.ShouldDrawWires(true)
				.IndentAmount(32)
				.BaseIndentLevel(0)
			];

		if (DebuggerModel->SelectedArchetypes.Num())
		{
			if (InEntryItem->bIsSelected)
			{
				Box->AddSlot()
				[
					SNew(STextBlock)
					.Text(FText::FromString(InEntryItem->Label))
					.ColorAndOpacity(FLinearColor::Green)
				];
			}
			else
			{
				const float DistanceToSelected = DebuggerModel->MinDistanceToSelectedArchetypes(InEntryItem);
				const FLinearColor LerpedColor = UE::Mass::Debugger::UI::Private::GetArchetypeDistanceColor(DistanceToSelected);

				Box->AddSlot()
				[
					SNew(STextBlock)
					.Text(FText::FromString(InEntryItem->Label))
					.ColorAndOpacity(LerpedColor)
				];
			}
		}
		else
		{
			Box->AddSlot()
			[
				SNew(STextBlock)
				.Text(FText::FromString(InEntryItem->Label))
			];
		}

		ChildSlot
		[
			Box.ToSharedRef()
		];
	}
};

//----------------------------------------------------------------------//
// SMassArchetypeDetailTableRow
//----------------------------------------------------------------------//
class SMassArchetypeDetailTableRow : public SMassArchetypeTableRowBase
{
public:
	void Construct(const FArguments& InArgs, const TSharedPtr<STableViewBase>& InOwnerTableView, const FMassDebuggerArchetypeDataPtr InEntryItem)
	{
		SMassArchetypeTableRowBase::Construct(InArgs, InOwnerTableView, InEntryItem);

		ChildSlot
			[
				SNew(SMassArchetype, InEntryItem)
			];
	}
};

//----------------------------------------------------------------------//
// SMassArchetypesView
//----------------------------------------------------------------------//
void SMassArchetypesView::Construct(const FArguments& InArgs, TSharedRef<FMassDebuggerModel> InDebuggerModel)
{
	Initialize(InDebuggerModel);

	ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.2f)
			[
				SAssignNew(ArchetypesTreeView, STreeView<TSharedPtr<FMassDebuggerArchetypeData>>)
				.TreeItemsSource(&TreeViewSource)
				.OnGenerateRow_Lambda([this](TSharedPtr<FMassDebuggerArchetypeData> Item, const TSharedPtr<STableViewBase>& OwnerTable)
					{
						return SNew(SMassArchetypeTableRow, OwnerTable, Item, DebuggerModel);
					})
				.OnGetChildren_Lambda([](TSharedPtr<FMassDebuggerArchetypeData> InItem, TArray<TSharedPtr<FMassDebuggerArchetypeData>>& OutChildren)
					{
						if (InItem->Children.Num())
						{
							OutChildren.Append(InItem->Children);
						}
					})
				.OnSelectionChanged(this, &SMassArchetypesView::HandleSelectionChanged)
			]

			+ SHorizontalBox::Slot()
			[
				SAssignNew(SelectedArchetypesListWidget, SListView<TSharedPtr<FMassDebuggerArchetypeData>>)
				.ListItemsSource(&DebuggerModel->SelectedArchetypes)
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow_Lambda([](TSharedPtr<FMassDebuggerArchetypeData> Item, const TSharedPtr<STableViewBase>& OwnerTable)
					{
						return SNew(SMassArchetypeDetailTableRow, OwnerTable, Item);
					})
			]
		];
}

void SMassArchetypesView::HandleSelectionChanged(TSharedPtr<FMassDebuggerArchetypeData> InNode, ESelectInfo::Type InSelectInfo)
{
	if (!DebuggerModel || InSelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	TArray<TSharedPtr<FMassDebuggerArchetypeData>> SelectedArchetypes;
	ArchetypesTreeView->GetSelectedItems(SelectedArchetypes);
	DebuggerModel->SelectArchetypes(SelectedArchetypes, InSelectInfo);
}

void SMassArchetypesView::OnRefresh()
{
	if (DebuggerModel)
	{
		TreeViewSource.Reset();
		
		TMap<uint32, TSharedPtr<FMassDebuggerArchetypeData>> ArchetypeRepresentativeMap;
		for (const TSharedPtr<FMassDebuggerArchetypeData>& ArchetypeData : DebuggerModel->CachedArchetypes)
		{
			if (TSharedPtr<FMassDebuggerArchetypeData>* Representative = ArchetypeRepresentativeMap.Find(ArchetypeData->FamilyHash))
			{
				(*Representative)->Children.Add(ArchetypeData);
			}
			else
			{
				ArchetypeRepresentativeMap.Add(ArchetypeData->FamilyHash, ArchetypeData);
			}
		}

		for (auto& KVP : ArchetypeRepresentativeMap)
		{
			TreeViewSource.Add(KVP.Value);
			ArchetypesTreeView->SetItemExpansion(KVP.Value, true);
		}
	}

	ArchetypesTreeView->RequestListRefresh();
	SelectedArchetypesListWidget->RequestListRefresh();
}

void SMassArchetypesView::OnProcessorsSelected(TConstArrayView<TSharedPtr<FMassDebuggerProcessorData>> SelectedProcessors, ESelectInfo::Type SelectInfo)
{
	if (DebuggerModel)
	{
		OnArchetypesSelected(DebuggerModel->SelectedArchetypes, ESelectInfo::Direct);
	}
}

void SMassArchetypesView::OnArchetypesSelected(TConstArrayView<TSharedPtr<FMassDebuggerArchetypeData>> SelectedArchetypes, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		ArchetypesTreeView->ClearSelection();
		if (SelectedArchetypes.Num())
		{
			for (const TSharedPtr<FMassDebuggerArchetypeData>& ArchetypeData : SelectedArchetypes)
			{
				ArchetypesTreeView->SetItemSelection(ArchetypeData, true);
			}

			// scroll to the first item to make sure there's anything in view
			ArchetypesTreeView->RequestScrollIntoView(SelectedArchetypes[0]);
		}
	}
	ArchetypesTreeView->RebuildList();
	SelectedArchetypesListWidget->RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE
