// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimerTreeView.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SScrollBox.h"
//#include "Widgets/Views/STableViewBase.h"

// Insights
#include "Insights/Widgets/STimersViewTooltip.h"
#include "Insights/Widgets/STimerTableRow.h"

#define LOCTEXT_NAMESPACE "STimerTreeView"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName STimerTreeView::TimerNameColumnId(TEXT("Name")); // FTimersViewColumns::TotalInclusiveTimeColumnID
const FName STimerTreeView::CountColumnId(TEXT("Count")); // FTimersViewColumns::TotalInclusiveTimeColumnID
const FName STimerTreeView::InclusiveTimeColumnId(TEXT("TotalInclTime")); // FTimersViewColumns::TotalInclusiveTimeColumnID
const FName STimerTreeView::ExclusiveTimeColumnId(TEXT("TotalExclTime")); // FTimersViewColumns::TotalExclusiveTimeColumnID

////////////////////////////////////////////////////////////////////////////////////////////////////

STimerTreeView::STimerTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STimerTreeView::~STimerTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void STimerTreeView::Construct(const FArguments& InArgs, const FText& InViewName)
{
	ViewName = InViewName;

	SAssignNew(ExternalScrollbar, SScrollBar)
	.AlwaysShowScrollbar(true);

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)

			+ SScrollBox::Slot()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(0.0f)
				[
					SAssignNew(TreeView, STreeView<FTimerNodePtr>)
					.ExternalScrollbar(ExternalScrollbar)
					.SelectionMode(ESelectionMode::Multi)
					.TreeItemsSource(&TreeNodes)
					.OnGetChildren(this, &STimerTreeView::TreeView_OnGetChildren)
					.OnGenerateRow(this, &STimerTreeView::TreeView_OnGenerateRow)
					//.OnSelectionChanged(this, &STimerTreeView::TreeView_OnSelectionChanged)
					//.OnMouseButtonDoubleClick(this, &STimerTreeView::TreeView_OnMouseButtonDoubleClick)
					//.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &STimerTreeView::TreeView_GetMenuContent))
					.ItemHeight(12.0f)
					.HeaderRow
					(
						SAssignNew(TreeViewHeaderRow, SHeaderRow)
						.Visibility(EVisibility::Visible)
					)
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f)
		[
			SNew(SBox)
			.WidthOverride(FOptionalSize(13.0f))
			[
				ExternalScrollbar.ToSharedRef()
			]
		]
	];

	InitializeAndShowHeaderColumns();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> STimerTreeView::TreeView_GetMenuContent()
{
	const TArray<FTimerNodePtr> SelectedTimerNodes = TreeView->GetSelectedItems();
	const int32 NumSelectedTimerNodes = SelectedTimerNodes.Num();
	FTimerNodePtr SelectedTimerNode = NumSelectedTimerNodes ? SelectedTimerNodes[0] : nullptr;

	FText SelectionStr;
	if (NumSelectedTimerNodes == 0)
	{
		SelectionStr = LOCTEXT("NothingSelected", "Nothing selected");
	}
	else if (NumSelectedTimerNodes == 1)
	{
		SelectionStr = FText::FromName(SelectedTimerNode->GetName());
	}
	else
	{
		SelectionStr = LOCTEXT("MultipleSelection", "Multiple selection");
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	// Selection menu
	MenuBuilder.BeginSection("Selection", LOCTEXT("ContextMenu_Header_Selection", "Selection"));
	{
		struct FLocal
		{
			static bool ReturnFalse()
			{
				return false;
			}
		};

		FUIAction DummyUIAction;
		DummyUIAction.CanExecuteAction = FCanExecuteAction::CreateStatic(&FLocal::ReturnFalse);
		MenuBuilder.AddMenuEntry
		(
			SelectionStr,
			LOCTEXT("ContextMenu_Selection", "Currently selected items"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "@missing.icon"), DummyUIAction, NAME_None, EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::InitializeAndShowHeaderColumns()
{
	TreeViewHeaderRow_CreateTimerNameColumnArgs();
	TreeViewHeaderRow_CreateCountColumnArgs();
	TreeViewHeaderRow_CreateInclusiveTimeColumnArgs();
	TreeViewHeaderRow_CreateExclusiveTimeColumnArgs();

	TreeViewHeaderRow->AddColumn(TreeViewHeaderColumnArgs[TimerNameColumnId]);
	TreeViewHeaderRow->AddColumn(TreeViewHeaderColumnArgs[CountColumnId]);
	TreeViewHeaderRow->AddColumn(TreeViewHeaderColumnArgs[InclusiveTimeColumnId]);
	TreeViewHeaderRow->AddColumn(TreeViewHeaderColumnArgs[ExclusiveTimeColumnId]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeViewHeaderRow_CreateTimerNameColumnArgs()
{
	SHeaderRow::FColumn::FArguments ColumnArgs;

	const FName& ColumnId = TimerNameColumnId;

	ColumnArgs
		.ColumnId(ColumnId)
		.DefaultLabel(ViewName)
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.ManualWidth(246.0f)
		.HeaderContent()
		[
			SNew(SBox)
			//.ToolTip(STimerTreeView::GetColumnTooltip(ColumnId))
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(ViewName)
			]
		]
		//.MenuContent()
		//[
		//	TreeViewHeaderRow_GenerateColumnMenu(ColumnId)
		//]
	;

	TreeViewHeaderColumnArgs.Add(ColumnId, ColumnArgs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeViewHeaderRow_CreateCountColumnArgs()
{
	SHeaderRow::FColumn::FArguments ColumnArgs;

	const FName& ColumnId = CountColumnId;
	const FText ColumnName = LOCTEXT("CountColumnName", "Count");

	ColumnArgs
		.ColumnId(ColumnId)
		.DefaultLabel(ColumnName)
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.ManualWidth(60.0f)
		.HeaderContent()
		[
			SNew(SBox)
			//.ToolTip(STimerTreeView::GetColumnTooltip(ColumnId))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(ColumnName)
			]
		]
		//.MenuContent()
		//[
		//	TreeViewHeaderRow_GenerateColumnMenu(ColumnId)
		//]
	;

	TreeViewHeaderColumnArgs.Add(ColumnId, ColumnArgs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeViewHeaderRow_CreateInclusiveTimeColumnArgs()
{
	SHeaderRow::FColumn::FArguments ColumnArgs;

	const FName& ColumnId = InclusiveTimeColumnId;
	const FText ColumnName = LOCTEXT("InclusiveTimeColumnName", "Incl");

	ColumnArgs
		.ColumnId(ColumnId)
		.DefaultLabel(ColumnName)
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.ManualWidth(60.0f)
		.HeaderContent()
		[
			SNew(SBox)
			//.ToolTip(STimerTreeView::GetColumnTooltip(ColumnId))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(ColumnName)
			]
		]
		//.MenuContent()
		//[
		//	TreeViewHeaderRow_GenerateColumnMenu(ColumnId)
		//]
	;

	TreeViewHeaderColumnArgs.Add(ColumnId, ColumnArgs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeViewHeaderRow_CreateExclusiveTimeColumnArgs()
{
	SHeaderRow::FColumn::FArguments ColumnArgs;

	const FName& ColumnId = ExclusiveTimeColumnId;
	const FText ColumnName = LOCTEXT("ExclusiveTimeColumnName", "Excl");

	ColumnArgs
		.ColumnId(ColumnId)
		.DefaultLabel(ColumnName)
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.ManualWidth(60.0f)
		.HeaderContent()
		[
			SNew(SBox)
			//.ToolTip(STimerTreeView::GetColumnTooltip(ColumnId))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(ColumnName)
			]
		]
		//.MenuContent()
		//[
		//	TreeViewHeaderRow_GenerateColumnMenu(ColumnId)
		//]
	;

	TreeViewHeaderColumnArgs.Add(ColumnId, ColumnArgs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TreeView
////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeView_Refresh()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeView_OnSelectionChanged(FTimerNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeView_OnGetChildren(FTimerNodePtr InParent, TArray<FTimerNodePtr>& OutChildren)
{
	constexpr bool bUseFiltering = false;
	if (bUseFiltering)
	{
		const TArray<Insights::FBaseTreeNodePtr>& Children = InParent->GetFilteredChildren();
		OutChildren.Reset(Children.Num());
		for (const Insights::FBaseTreeNodePtr& Child : Children)
		{
			OutChildren.Add(StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(Child));
		}
	}
	else
	{
		const TArray<Insights::FBaseTreeNodePtr>& Children = InParent->GetChildren();
		OutChildren.Reset(Children.Num());
		for (const Insights::FBaseTreeNodePtr& Child : Children)
		{
			OutChildren.Add(StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(Child));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TreeView_OnMouseButtonDoubleClick(FTimerNodePtr TimerNode)
{
	if (TimerNode->IsGroup())
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(TimerNode);
		TreeView->SetItemExpansion(TimerNode, !bIsGroupExpanded);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View's Table Row
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> STimerTreeView::TreeView_OnGenerateRow(FTimerNodePtr TimerNodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(STimerTableRow, OwnerTable)
		.OnShouldBeEnabled(this, &STimerTreeView::TableRow_ShouldBeEnabled)
		.OnIsColumnVisible(this, &STimerTreeView::TableRow_IsColumnVisible)
		.OnSetHoveredTableCell(this, &STimerTreeView::TableRow_SetHoveredTableCell)
		.OnGetColumnOutlineHAlignmentDelegate(this, &STimerTreeView::TableRow_GetColumnOutlineHAlignment)
		.HighlightText(this, &STimerTreeView::TableRow_GetHighlightText)
		.HighlightedNodeName(this, &STimerTreeView::TableRow_GetHighlightedNodeName)
		.TimerNodePtr(TimerNodePtr);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimerTreeView::TableRow_ShouldBeEnabled(const uint32 TimerId) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimerTreeView::TableRow_IsColumnVisible(const FName ColumnId) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::TableRow_SetHoveredTableCell(const FName ColumnId, const FTimerNodePtr TimerNodePtr)
{
	HoveredColumnId = ColumnId;

	const bool bIsAnyMenusVisible = FSlateApplication::Get().AnyMenusVisible();
	if (!HasMouseCapture() && !bIsAnyMenusVisible)
	{
		HoveredTimerNodePtr = TimerNodePtr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EHorizontalAlignment STimerTreeView::TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const
{
	// First column
	if (ColumnId == TimerNameColumnId)
	{
		return HAlign_Left;
	}
	// Last column
	else if (ColumnId == ExclusiveTimeColumnId)
	{
		return HAlign_Right;
	}
	// Middle columns
	{
		return HAlign_Center;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STimerTreeView::TableRow_GetHighlightText() const
{
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName STimerTreeView::TableRow_GetHighlightedNodeName() const
{
	return HighlightedNodeName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

//TSharedPtr<SWidget> STimerTreeView::TreeView_GetMenuContent()
//{
//}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::Reset()
{
	TreeNodes.Reset();
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::SetTree(const Trace::FTimingProfilerButterflyNode& Root)
{
	TreeNodes.Reset();

	FTimerNodePtr RootTimerNodePtr = CreateTimerNodeRec(Root);
	if (RootTimerNodePtr)
	{
		TreeNodes.Add(RootTimerNodePtr);
	}

	TreeView_Refresh();

	if (RootTimerNodePtr)
	{
		ExpandNodesRec(RootTimerNodePtr, 0);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNodePtr STimerTreeView::CreateTimerNodeRec(const Trace::FTimingProfilerButterflyNode& Node)
{
	if (Node.Timer == nullptr)
	{
		return nullptr;
	}

	const uint64 Id = Node.Timer->Id;
	const FName Name(Node.Timer->Name);
	const FName Group(Node.Timer->IsGpuTimer ? TEXT("GPU") : TEXT("CPU"));
	const ETimerNodeType Type = Node.Timer->IsGpuTimer ? ETimerNodeType::GpuScope : ETimerNodeType::CpuScope;

	FTimerNodePtr TimerNodePtr = MakeShareable(new FTimerNode(Id, Name, Group, Type));

	Trace::FAggregatedTimingStats AggregatedStats;
	AggregatedStats.InstanceCount = Node.Count;
	AggregatedStats.TotalInclusiveTime = Node.InclusiveTime;
	AggregatedStats.TotalExclusiveTime = Node.ExclusiveTime;
	TimerNodePtr->SetAggregatedStats(AggregatedStats);

	for (const Trace::FTimingProfilerButterflyNode* ChildNodePtr : Node.Children)
	{
		if (ChildNodePtr != nullptr)
		{
			FTimerNodePtr ChildTimerNodePtr = CreateTimerNodeRec(*ChildNodePtr);
			if (ChildTimerNodePtr)
			{
				TimerNodePtr->AddChildAndSetGroupPtr(ChildTimerNodePtr);
			}
		}
	}

	// Sort children by InclTime (descending).
	TArray<Insights::FBaseTreeNodePtr>& Children = const_cast<TArray<Insights::FBaseTreeNodePtr>&>(TimerNodePtr->GetChildren());
	Children.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
	{
		const double InclTimeA = StaticCastSharedPtr<FTimerNode>(A)->GetAggregatedStats().TotalInclusiveTime;
		const double InclTimeB = StaticCastSharedPtr<FTimerNode>(B)->GetAggregatedStats().TotalInclusiveTime;
		return InclTimeA >= InclTimeB;
	});

	return TimerNodePtr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimerTreeView::ExpandNodesRec(FTimerNodePtr NodePtr, int32 Depth)
{
	//constexpr int32 MaxDepth = 3;

	TreeView->SetItemExpansion(NodePtr, true);

	//if (Depth < MaxDepth)
	{
		for (const Insights::FBaseTreeNodePtr& ChildPtr : NodePtr->GetChildren())
		{
			ExpandNodesRec(StaticCastSharedPtr<FTimerNode>(ChildPtr), Depth + 1);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
