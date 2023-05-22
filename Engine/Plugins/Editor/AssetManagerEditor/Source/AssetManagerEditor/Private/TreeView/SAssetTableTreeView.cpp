// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetTableTreeView.h"

#include "Containers/Set.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Modules/ModuleManager.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SToolTip.h"
#include "DesktopPlatformModule.h"
#include "HAL/PlatformFileManager.h"
#include "Logging/MessageLog.h"

#include "Insights/Common/InsightsStyle.h"
#include "Insights/Common/Log.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Filter/ViewModels/FilterConfigurator.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TreeNodeGrouping.h"
#include "TreeView/AssetTable.h"
#include "TreeView/AssetTreeNode.h"
#include "TreeView/AssetDependencyGrouping.h"

#include <limits>
#include <memory>

#define LOCTEXT_NAMESPACE "SAssetTableTreeView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SAssetTableTreeView::SAssetTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SAssetTableTreeView::~SAssetTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::Construct(const FArguments& InArgs, TSharedPtr<FAssetTable> InTablePtr)
{
	this->OnSelectionChanged = InArgs._OnSelectionChanged;

	STableTreeView::ConstructWidget(InTablePtr);

	CreateGroupings();
	CreateSortings();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::Reset()
{
	//...
	UE::Insights::STableTreeView::Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UE::Insights::STableTreeView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bNeedsToRebuild && !bIsUpdateRunning)
	{
		RebuildTree(true);
		bNeedsToRebuild = false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::RebuildTree(bool bResync)
{
	UE::Insights::FStopwatch Stopwatch;
	Stopwatch.Start();

	UE::Insights::FStopwatch SyncStopwatch;
	SyncStopwatch.Start();

	if (bResync)
	{
		TableTreeNodes.Empty();
	}

	const int32 PreviousNodeCount = TableTreeNodes.Num();

	TSharedPtr<FAssetTable> AssetTable = GetAssetTable();
	const TArray<FAssetTableRow>& Assets = AssetTable->GetAssets();
	const int32 VisibleAssetCount = AssetTable->GetVisibleAssetCount();

	if (VisibleAssetCount != PreviousNodeCount)
	{
		UE_LOG(LogInsights, Log, TEXT("[AssetTree] Creating nodes (%d nodes --> %d assets)..."), PreviousNodeCount, VisibleAssetCount);

		if (VisibleAssetCount < PreviousNodeCount)
		{
			TableTreeNodes.Empty();
		}
		TableTreeNodes.Reserve(VisibleAssetCount);

		//const FName BaseNodeName(TEXT("asset"));
		for (int32 AssetIndex = TableTreeNodes.Num(); AssetIndex < VisibleAssetCount; ++AssetIndex)
		{
			const FAssetTableRow* Asset = AssetTable->GetAsset(AssetIndex);

			//FName NodeName(BaseNodeName, AssetIndex + 1);
			FName NodeName(Asset->GetName());
			FAssetTreeNodePtr NodePtr = MakeShared<FAssetTreeNode>(NodeName, AssetTable, AssetIndex);
			TableTreeNodes.Add(NodePtr);
		}
		ensure(TableTreeNodes.Num() == VisibleAssetCount);
	}

	SyncStopwatch.Stop();

	if (bResync || TableTreeNodes.Num() != PreviousNodeCount)
	{
		// Save selection.
		TArray<UE::Insights::FTableTreeNodePtr> SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		UE_LOG(LogInsights, Log, TEXT("[AssetTree] Update tree..."));
		UpdateTree();

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			for (UE::Insights::FTableTreeNodePtr& NodePtr : SelectedItems)
			{
				NodePtr = GetNodeByTableRowIndex(NodePtr->GetRowIndex());
			}
			SelectedItems.RemoveAll([](const UE::Insights::FTableTreeNodePtr& NodePtr) { return !NodePtr.IsValid(); });
			if (SelectedItems.Num() > 0)
			{
				TreeView->SetItemSelection(SelectedItems, true);
				TreeView->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	const double SyncTime = SyncStopwatch.GetAccumulatedTime();
	UE_LOG(LogInsights, Log, TEXT("[AssetTree] Tree view rebuilt in %.4fs (sync: %.4fs + update: %.4fs) --> %d nodes (%d added)"),
		TotalTime, SyncTime, TotalTime - SyncTime, TableTreeNodes.Num(), TableTreeNodes.Num() - PreviousNodeCount);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SAssetTableTreeView::IsRunning() const
{
	return STableTreeView::IsRunning();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double SAssetTableTreeView::GetAllOperationsDuration()
{
	return STableTreeView::GetAllOperationsDuration();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAssetTableTreeView::GetCurrentOperationName() const
{
	return STableTreeView::GetCurrentOperationName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> SAssetTableTreeView::ConstructToolbar()
{
	TSharedPtr<SHorizontalBox> Box = SNew(SHorizontalBox);

	Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Preset", "Preset:"))
		];

	Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(150.0f)
			[
				SAssignNew(PresetComboBox, SComboBox<TSharedRef<UE::Insights::ITableTreeViewPreset>>)
				.ToolTipText(this, &SAssetTableTreeView::ViewPreset_GetSelectedToolTipText)
				.OptionsSource(GetAvailableViewPresets())
				.OnSelectionChanged(this, &SAssetTableTreeView::ViewPreset_OnSelectionChanged)
				.OnGenerateWidget(this, &SAssetTableTreeView::ViewPreset_OnGenerateWidget)
				[
					SNew(STextBlock)
					.Text(this, &SAssetTableTreeView::ViewPreset_GetSelectedText)
				]
			]
		];

	return Box;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::InitAvailableViewPresets()
{
	//////////////////////////////////////////////////
	// Default View

	class FDefaultViewPreset : public UE::Insights::ITableTreeViewPreset
	{
	public:
		virtual FText GetName() const override
		{
			return LOCTEXT("Default_PresetName", "Default");
		}
		virtual FText GetToolTip() const override
		{
			return LOCTEXT("Default_PresetToolTip", "Default View\nConfigure the tree view to show default asset info.");
		}
		virtual FName GetSortColumn() const override
		{
			return UE::Insights::FTable::GetHierarchyColumnId();
		}
		virtual EColumnSortMode::Type GetSortMode() const override
		{
			return EColumnSortMode::Type::Ascending;
		}
		virtual void SetCurrentGroupings(const TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InAvailableGroupings, TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InOutCurrentGroupings) const override
		{
			InOutCurrentGroupings.Reset();

			check(InAvailableGroupings[0]->Is<UE::Insights::FTreeNodeGroupingFlat>());
			InOutCurrentGroupings.Add(InAvailableGroupings[0]);
		}
		virtual void GetColumnConfigSet(TArray<UE::Insights::FTableColumnConfig>& InOutConfigSet) const override
		{
			InOutConfigSet.Add({ UE::Insights::FTable::GetHierarchyColumnId(),              true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::CountColumnId,                         true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TypeColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NameColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PathColumnId,                         !true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryTypeColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryNameColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::StagedCompressedSizeColumnId,          true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeUniqueDependenciesColumnId,  !true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeOtherDependenciesColumnId,   !true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalUsageCountColumnId,               true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::ChunksColumnId,                       !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NativeClassColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::GameFeaturePluginColumnId,             true, 200.0f });
		}
	};
	AvailableViewPresets.Add(MakeShared<FDefaultViewPreset>());

	//////////////////////////////////////////////////
	// GameFeaturePlugin, PrimaryType, Dependency View
	
	class FGameFeaturePluginPrimaryTypeDependencyView : public UE::Insights::ITableTreeViewPreset
	{
	public:
		virtual FText GetName() const override
		{
			return LOCTEXT("GFPTypeDepView_PresetName", "Dependency Analysis");
		}

		virtual FText GetToolTip() const override
		{
			return LOCTEXT("GFPTypeDepView_PresetToolTip", "Dependency Analysis View\nConfigure the tree view to show a breakdown of assets by Game Feature Plugin, Primary Asset Type, and Dependencies.");
		}
		virtual FName GetSortColumn() const override
		{
			return UE::Insights::FTable::GetHierarchyColumnId();
		}
		virtual EColumnSortMode::Type GetSortMode() const override
		{
			return EColumnSortMode::Type::Ascending;
		}
		virtual void SetCurrentGroupings(const TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InAvailableGroupings, TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InOutCurrentGroupings) const override
		{
			InOutCurrentGroupings.Reset();

			check(InAvailableGroupings[0]->Is<UE::Insights::FTreeNodeGroupingFlat>());
			InOutCurrentGroupings.Add(InAvailableGroupings[0]);

			const TSharedPtr<UE::Insights::FTreeNodeGrouping>* GameFeaturePluginGrouping = InAvailableGroupings.FindByPredicate(
				[](TSharedPtr<UE::Insights::FTreeNodeGrouping>& Grouping)
				{
					return Grouping->Is<UE::Insights::FTreeNodeGroupingByUniqueValueCString>() &&
						Grouping->As<UE::Insights::FTreeNodeGroupingByUniqueValueCString>().GetColumnId() == FAssetTableColumns::GameFeaturePluginColumnId;
				});
			if (GameFeaturePluginGrouping)
			{
				InOutCurrentGroupings.Add(*GameFeaturePluginGrouping);
			}

			const TSharedPtr<UE::Insights::FTreeNodeGrouping>* PrimaryTypeGrouping = InAvailableGroupings.FindByPredicate(
				[](TSharedPtr<UE::Insights::FTreeNodeGrouping>& Grouping)
				{
					return Grouping->Is<UE::Insights::FTreeNodeGroupingByUniqueValueCString>() &&
						Grouping->As<UE::Insights::FTreeNodeGroupingByUniqueValueCString>().GetColumnId() == FAssetTableColumns::PrimaryTypeColumnId;
				});
			if (PrimaryTypeGrouping)
			{
				InOutCurrentGroupings.Add(*PrimaryTypeGrouping);
			}

			const TSharedPtr<UE::Insights::FTreeNodeGrouping>* DependencyGrouping = InAvailableGroupings.FindByPredicate(
				[](TSharedPtr<UE::Insights::FTreeNodeGrouping>& Grouping)
				{
					return Grouping->Is<FAssetDependencyGrouping>();
				});
			if (DependencyGrouping)
			{
				InOutCurrentGroupings.Add(*DependencyGrouping);
			}
		}
		virtual void GetColumnConfigSet(TArray<UE::Insights::FTableColumnConfig>& InOutConfigSet) const override
		{
			InOutConfigSet.Add({ UE::Insights::FTable::GetHierarchyColumnId(),              true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::CountColumnId,                         true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TypeColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NameColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PathColumnId,                         !true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryTypeColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryNameColumnId,                  !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::StagedCompressedSizeColumnId,          true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeUniqueDependenciesColumnId,   true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeOtherDependenciesColumnId,    true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalUsageCountColumnId,              !true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::ChunksColumnId,                       !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NativeClassColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::GameFeaturePluginColumnId,             true, 200.0f });
		}
	};
	AvailableViewPresets.Add(MakeShared<FGameFeaturePluginPrimaryTypeDependencyView>());

	//////////////////////////////////////////////////
	// Path Breakdown View

	class FAssetPathViewPreset : public UE::Insights::ITableTreeViewPreset
	{
	public:
		virtual FText GetName() const override
		{
			return LOCTEXT("Path_PresetName", "Path");
		}
		virtual FText GetToolTip() const override
		{
			return LOCTEXT("Path_PresetToolTip", "Path Breakdown View\nConfigure the tree view to show a breakdown of assets by their path.");
		}
		virtual FName GetSortColumn() const override
		{
			return UE::Insights::FTable::GetHierarchyColumnId();
		}
		virtual EColumnSortMode::Type GetSortMode() const override
		{
			return EColumnSortMode::Type::Ascending;
		}
		virtual void SetCurrentGroupings(const TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InAvailableGroupings, TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InOutCurrentGroupings) const override
		{
			InOutCurrentGroupings.Reset();

			check(InAvailableGroupings[0]->Is<UE::Insights::FTreeNodeGroupingFlat>());
			InOutCurrentGroupings.Add(InAvailableGroupings[0]);

			const TSharedPtr<UE::Insights::FTreeNodeGrouping>* PathGrouping = InAvailableGroupings.FindByPredicate(
				[](TSharedPtr<UE::Insights::FTreeNodeGrouping>& Grouping)
				{
					return Grouping->Is<UE::Insights::FTreeNodeGroupingByPathBreakdown>() &&
						   Grouping->As<UE::Insights::FTreeNodeGroupingByPathBreakdown>().GetColumnId() == FAssetTableColumns::PathColumnId;
				});
			if (PathGrouping)
			{
				InOutCurrentGroupings.Add(*PathGrouping);
			}
		}
		virtual void GetColumnConfigSet(TArray<UE::Insights::FTableColumnConfig>& InOutConfigSet) const override
		{
			InOutConfigSet.Add({ UE::Insights::FTable::GetHierarchyColumnId(),              true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::CountColumnId,                         true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TypeColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NameColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PathColumnId,                         !true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryTypeColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryNameColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::StagedCompressedSizeColumnId,          true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeUniqueDependenciesColumnId,   true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeOtherDependenciesColumnId,    true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalUsageCountColumnId,               true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::ChunksColumnId,                       !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NativeClassColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::GameFeaturePluginColumnId,             true, 200.0f });
		}
	};
	AvailableViewPresets.Add(MakeShared<FAssetPathViewPreset>());

	//////////////////////////////////////////////////
	// Primary Asset Breakdown View

	class FPrimaryAssetViewPreset : public UE::Insights::ITableTreeViewPreset
	{
	public:
		virtual FText GetName() const override
		{
			return LOCTEXT("PrimaryAsset_PresetName", "Primary Asset");
		}
		virtual FText GetToolTip() const override
		{
			return LOCTEXT("PrimaryAsset_PresetToolTip", "Primary Asset Breakdown View\nConfigure the tree view to show a breakdown of assets by their primary asset type/name.");
		}
		virtual FName GetSortColumn() const override
		{
			return UE::Insights::FTable::GetHierarchyColumnId();
		}
		virtual EColumnSortMode::Type GetSortMode() const override
		{
			return EColumnSortMode::Type::Ascending;
		}
		virtual void SetCurrentGroupings(const TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InAvailableGroupings, TArray<TSharedPtr<UE::Insights::FTreeNodeGrouping>>& InOutCurrentGroupings) const override
		{
			InOutCurrentGroupings.Reset();

			check(InAvailableGroupings[0]->Is<UE::Insights::FTreeNodeGroupingFlat>());
			InOutCurrentGroupings.Add(InAvailableGroupings[0]);

			const TSharedPtr<UE::Insights::FTreeNodeGrouping>* PrimaryTypeGrouping = InAvailableGroupings.FindByPredicate(
				[](TSharedPtr<UE::Insights::FTreeNodeGrouping>& Grouping)
				{
					return Grouping->Is<UE::Insights::FTreeNodeGroupingByUniqueValueCString>() &&
						   Grouping->As<UE::Insights::FTreeNodeGroupingByUniqueValueCString>().GetColumnId() == FAssetTableColumns::PrimaryTypeColumnId;
				});
			if (PrimaryTypeGrouping)
			{
				InOutCurrentGroupings.Add(*PrimaryTypeGrouping);
			}
		}
		virtual void GetColumnConfigSet(TArray<UE::Insights::FTableColumnConfig>& InOutConfigSet) const override
		{
			InOutConfigSet.Add({ UE::Insights::FTable::GetHierarchyColumnId(),              true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::CountColumnId,                         true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TypeColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NameColumnId,                         !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PathColumnId,                         !true, 400.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryTypeColumnId,                  !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::PrimaryNameColumnId,                  !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::StagedCompressedSizeColumnId,          true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeUniqueDependenciesColumnId,   true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeOtherDependenciesColumnId,    true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalUsageCountColumnId,               true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::ChunksColumnId,                       !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NativeClassColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::GameFeaturePluginColumnId,             true, 200.0f });
		}
	};
	AvailableViewPresets.Add(MakeShared<FPrimaryAssetViewPreset>());

	//////////////////////////////////////////////////

	SelectedViewPreset = AvailableViewPresets[0];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> SAssetTableTreeView::ConstructFooter()
{
	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SAssetTableTreeView::GetFooterLeftText)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 2.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SAssetTableTreeView::GetFooterCenterText1)
			.ColorAndOpacity(FSlateColor(EStyleColor::White25))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 2.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SAssetTableTreeView::GetFooterCenterText2)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 2.0f, 2.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SAssetTableTreeView::GetFooterRightText1)
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::InternalCreateGroupings()
{
	UE::Insights::STableTreeView::InternalCreateGroupings();

	AvailableGroupings.RemoveAll(
		[](TSharedPtr<UE::Insights::FTreeNodeGrouping>& Grouping)
		{
			if (Grouping->Is<UE::Insights::FTreeNodeGroupingByUniqueValue>())
			{
				const FName ColumnId = Grouping->As<UE::Insights::FTreeNodeGroupingByUniqueValue>().GetColumnId();
				if (ColumnId == FAssetTableColumns::CountColumnId)
				{
					return true;
				}
			}
			else if (Grouping->Is<UE::Insights::FTreeNodeGroupingByPathBreakdown>())
			{
				const FName ColumnId = Grouping->As<UE::Insights::FTreeNodeGroupingByPathBreakdown>().GetColumnId();
				if (ColumnId != FAssetTableColumns::PathColumnId)
				{
					return true;
				}
			}
			return false;
		});

	// Add custom groupings...
	int32 Index = 1; // after the Flat ("All") grouping

	AvailableGroupings.Insert(MakeShared<FAssetDependencyGrouping>(), Index++);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SAssetTableTreeView::ApplyCustomAdvancedFilters(const UE::Insights::FTableTreeNodePtr& NodePtr)
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::AddCustomAdvancedFilters()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::ExtendMenu(FMenuBuilder& MenuBuilder)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAssetTableTreeView::GetFooterLeftText() const
{
	return FooterLeftText;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAssetTableTreeView::GetFooterCenterText1() const
{
	return FooterCenterText1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAssetTableTreeView::GetFooterCenterText2() const
{
	return FooterCenterText2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAssetTableTreeView::GetFooterRightText1() const
{
	return FooterRightText1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAssetTableTreeView::TreeView_OnSelectionChanged(UE::Insights::FTableTreeNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	TArray<UE::Insights::FTableTreeNodePtr> SelectedNodes;
	const int32 NumSelectedNodes = TreeView->GetSelectedItems(SelectedNodes);
	int32 NumSelectedAssets = 0;
	FAssetTreeNodePtr NewSelectedAssetNode;
	TSet<int32> SelectionSetIndices;

	for (const UE::Insights::FTableTreeNodePtr& Node : SelectedNodes)
	{
		if (Node->Is<FAssetTreeNode>() && !Node->IsGroup())
		{
			NewSelectedAssetNode = StaticCastSharedPtr<FAssetTreeNode>(Node);
			SelectionSetIndices.Add(NewSelectedAssetNode->GetRowId().RowIndex);
			++NumSelectedAssets;
		}
	}

	const int32 VisibleAssetCount = GetAssetTable()->GetVisibleAssetCount();
	if (NumSelectedAssets == 0)
	{
		FooterLeftText = FText::Format(LOCTEXT("FooterLeftTextFmt0", "{0} assets"), FText::AsNumber(VisibleAssetCount));
		FooterCenterText1 = FText();
		FooterCenterText2 = FText();
		FooterRightText1 = FText();
	}
	else if (NumSelectedAssets == 1)
	{
		FooterLeftText = FText::Format(LOCTEXT("FooterLeftTextFmt1", "{0} assets (1 selected)"), FText::AsNumber(VisibleAssetCount));

		const FAssetTableRow& AssetTableRow = NewSelectedAssetNode->GetAssetChecked();
		FooterCenterText1 = FText::FromString(AssetTableRow.GetPath());
		FooterCenterText2 = FText::FromString(AssetTableRow.GetName());
		FooterRightText1 = FText::Format(LOCTEXT("FooterRightFmt3", "Self: {0} Unique: {1} Shared: {2}"),
			FText::AsMemory(AssetTableRow.GetStagedCompressedSize()),
			FText::AsMemory(AssetTableRow.GetOrComputeTotalSizeUniqueDependencies(*GetAssetTable(), NewSelectedAssetNode->GetRowId().RowIndex)),
			FText::AsMemory(AssetTableRow.GetOrComputeTotalSizeOtherDependencies(*GetAssetTable(), NewSelectedAssetNode->GetRowId().RowIndex)));
	}
	else
	{
		FooterLeftText = FText::Format(LOCTEXT("FooterLeftTextFmt2", "{0} assets ({1} selected)"), FText::AsNumber(VisibleAssetCount), FText::AsNumber(NumSelectedAssets));
		FooterCenterText1 = FText();
		FooterCenterText2 = FText();

		int64 TotalSelfSize = 0;
		for (int32 Index : SelectionSetIndices)
		{
			TotalSelfSize += GetAssetTable()->GetAssetChecked(Index).GetStagedCompressedSize();
		}
		FAssetTableDependencySizes Sizes = FAssetTableRow::ComputeDependencySizes(*GetAssetTable(), SelectionSetIndices, nullptr, nullptr);
		FooterRightText1 = FText::Format(LOCTEXT("FooterRightFmt3", "Self: {0} Unique: {1} Shared: {2}"),
			FText::AsMemory(TotalSelfSize),
			FText::AsMemory(Sizes.UniqueDependenciesSize),
			FText::AsMemory(Sizes.OtherDependenciesSize));
	}

	if (NumSelectedAssets != 1)
	{
		NewSelectedAssetNode.Reset();
	}

	if (SelectedAssetNode != NewSelectedAssetNode)
	{
		SelectedAssetNode = NewSelectedAssetNode;
	}

	if (OnSelectionChanged.IsBound())
	{
		OnSelectionChanged.Execute(SelectedNodes);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
