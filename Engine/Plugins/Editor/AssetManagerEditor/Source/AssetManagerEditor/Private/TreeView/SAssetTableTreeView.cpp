// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetTableTreeView.h"

#include "Containers/Set.h"
#include "ContentBrowserModule.h"
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
#include "HAL/FileManager.h"
#include "Logging/MessageLog.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IContentBrowserSingleton.h"
#include "AssetManagerEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"

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

extern UNREALED_API UEditorEngine* GEditor;

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
		UE_LOG(LogInsights, Log, TEXT("[AssetTree] Update tree..."));
		UpdateTree();
		TreeView->RebuildList();
		TreeView->ClearSelection();
		SelectedIndices.Empty();
		SelectedAssetNode.Reset();
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
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeSharedDependenciesColumnId,   !true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeExternalDependenciesColumnId,!true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalUsageCountColumnId,               true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::ChunksColumnId,                       !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NativeClassColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::GameFeaturePluginColumnId,             true, 200.0f });
		}
	};
	AvailableViewPresets.Add(MakeShared<FDefaultViewPreset>());

	//////////////////////////////////////////////////
	// GameFeaturePlugin, Type, Dependency View
	
	class FGameFeaturePluginTypeDependencyView : public UE::Insights::ITableTreeViewPreset
	{
	public:
		virtual FText GetName() const override
		{
			return LOCTEXT("GFPTypeDepView_PresetName", "Dependency Analysis");
		}

		virtual FText GetToolTip() const override
		{
			return LOCTEXT("GFPTypeDepView_PresetToolTip", "Dependency Analysis View\nConfigure the tree view to show a breakdown of assets by Game Feature Plugin, Type, and Dependencies.");
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
						Grouping->As<UE::Insights::FTreeNodeGroupingByUniqueValueCString>().GetColumnId() == FAssetTableColumns::TypeColumnId;
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
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeSharedDependenciesColumnId,    true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeExternalDependenciesColumnId,!true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalUsageCountColumnId,              !true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::ChunksColumnId,                       !true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::NativeClassColumnId,                   true, 200.0f });
			InOutConfigSet.Add({ FAssetTableColumns::GameFeaturePluginColumnId,             true, 200.0f });
		}
	};
	AvailableViewPresets.Add(MakeShared<FGameFeaturePluginTypeDependencyView>());

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
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeSharedDependenciesColumnId,    true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeExternalDependenciesColumnId,!true, 100.0f });
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
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeSharedDependenciesColumnId,    true, 100.0f });
			InOutConfigSet.Add({ FAssetTableColumns::TotalSizeExternalDependenciesColumnId,!true, 100.0f });
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

void SAssetTableTreeView::ExportDependencyData() const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		void* ParentWindowHandle = (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid()) ? ParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

		FString DefaultFileName;
		if (SelectedIndices.Num() > 1)
		{
			DefaultFileName = "Batch Dependency Export.csv";
		}
		else
		{
			int32 RootIndex = *SelectedIndices.CreateConstIterator();
			DefaultFileName = FString::Printf(TEXT("%s Dependencies.csv"), GetAssetTable()->GetAssetChecked(RootIndex).GetName());
		}

		TArray<FString> SaveFileNames;
		const bool bFileSelected = DesktopPlatform->SaveFileDialog(
			ParentWindowHandle,
			LOCTEXT("ExportDependencyData_SaveFileDialogTitle", "Save dependency data as...").ToString(),
			FPaths::ProjectLogDir(),
			DefaultFileName,
			TEXT("Comma-Separated Values (*.csv)|*.csv"),
			EFileDialogFlags::None,
			SaveFileNames);
		
		if (!bFileSelected)
		{
			return;
		}
		ensure(SaveFileNames.Num() == 1);
		FString OutputFileName = SaveFileNames[0];


		//FString FolderPath;
		//const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
		//	ParentWindowHandle,
		//	LOCTEXT("FolderDialogTitle", "Choose a directory").ToString(),
		//	FPaths::ProjectLogDir(),
		//	FolderPath
		//);

		//if (bFolderSelected)
		//{
		//	if (!FolderPath.EndsWith(TEXT("/")))
		//	{
		//		FolderPath += TEXT("/");
		//	}
		//}

		TSet<int32> UniqueDependencies;
		TSet<int32> SharedDependencies;
		FAssetTableRow::ComputeDependencySizes(*GetAssetTable(), SelectedIndices, &UniqueDependencies, &SharedDependencies);

		TSet<int32> ExternalDependencies;
		FAssetTableRow::ComputeTotalSizeExternalDependencies(*GetAssetTable(), SelectedIndices, &ExternalDependencies);

		FString TimeSuffix = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

		TAnsiStringBuilder<4096> StringBuilder;
		StringBuilder.Appendf("Asset,Self Size,Dependency Type\n");
		{
			for (int32 RootIndex : SelectedIndices)
			{
				const FAssetTableRow& Row = GetAssetTable()->GetAssetChecked(RootIndex);
				StringBuilder.Appendf("%s%s,%s,Root\n", *WriteToAnsiString<512>(Row.GetPath()), *WriteToAnsiString<64>(Row.GetName()), *WriteToAnsiString<32>(LexToString(Row.GetStagedCompressedSize())));
			}

			for (int32 DependencyIndex : UniqueDependencies)
			{
				const FAssetTableRow& DependencyRow = GetAssetTable()->GetAssetChecked(DependencyIndex);
				StringBuilder.Appendf("%s%s,%s,Unique\n", *WriteToAnsiString<512>(DependencyRow.GetPath()), *WriteToAnsiString<64>(DependencyRow.GetName()), *WriteToAnsiString<32>(LexToString(DependencyRow.GetStagedCompressedSize())));
			}

			for (int32 DependencyIndex : SharedDependencies)
			{
				const FAssetTableRow& DependencyRow = GetAssetTable()->GetAssetChecked(DependencyIndex);
				StringBuilder.Appendf("%s%s,%s,Shared\n", *WriteToAnsiString<512>(DependencyRow.GetPath()), *WriteToAnsiString<64>(DependencyRow.GetName()), *WriteToAnsiString<32>(LexToString(DependencyRow.GetStagedCompressedSize())));
			}

			for (int32 DependencyIndex : ExternalDependencies)
			{
				const FAssetTableRow& DependencyRow = GetAssetTable()->GetAssetChecked(DependencyIndex);
				StringBuilder.Appendf("%s%s,%s,External\n", *WriteToAnsiString<512>(DependencyRow.GetPath()), *WriteToAnsiString<64>(DependencyRow.GetName()), *WriteToAnsiString<32>(LexToString(DependencyRow.GetStagedCompressedSize())));
			}
		}

		TUniquePtr<FArchive> DependencyFile(IFileManager::Get().CreateFileWriter(*OutputFileName));
		DependencyFile->Serialize(StringBuilder.GetData(), StringBuilder.Len());
	}
}

TArray<FAssetData> SAssetTableTreeView::GetAssetDataForSelection() const
{
	TArray<FAssetData> Assets;
	for (int32 SelectionIndex : SelectedIndices)
	{
		const FAssetManagerEditorRegistrySource* RegistrySource = IAssetManagerEditorModule::Get().GetCurrentRegistrySource();
		const FSoftObjectPath& SoftObjectPath = GetAssetTable()->GetAssetChecked(SelectionIndex).GetSoftObjectPath();
		FAssetData AssetData;
		if (RegistrySource->GetOwnedRegistryState())
		{
			AssetData = *RegistrySource->GetOwnedRegistryState()->GetAssetByObjectPath(SoftObjectPath);
		}
		else
		{
			// Once we move this out of asset audit browser window and give it its own registry state, this code path can be killed
			// it exists to handle the "Editor" case as a registry source
			AssetData = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetByObjectPath(SoftObjectPath, /*IncludeOnlyOnDiskAsset*/true, /*SkipARFilteredAssets*/false);
		}
		Assets.Add(AssetData);
	}

	return Assets;
}

void SAssetTableTreeView::ExtendMenu(FMenuBuilder& MenuBuilder)
{
	FCanExecuteAction HasSelectionAndCanExecute = FCanExecuteAction::CreateLambda([this]()
		{
			return GetAssetTable() && (SelectedIndices.Num() > 0);
		});

	FCanExecuteAction HasSelectionAndRegistrySourceAndCanExecute = FCanExecuteAction::CreateLambda([this]()
		{
			return (IAssetManagerEditorModule::Get().GetCurrentRegistrySource() != nullptr) && GetAssetTable() && (SelectedIndices.Num() > 0);
		});

	MenuBuilder.BeginSection("Asset", LOCTEXT("ContextMenu_Section_Asset", "Asset"));
	{
		//////////////////////////////////////////////////////////////////////////
		/// EditSelectedAssets

		FUIAction EditSelectedAssets;
		EditSelectedAssets.CanExecuteAction = HasSelectionAndCanExecute;
		EditSelectedAssets.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				TArray<FSoftObjectPath> AssetPaths;
				for (int32 SelectionIndex : SelectedIndices)
				{
					AssetPaths.Add(GetAssetTable()->GetAssetChecked(SelectionIndex).GetSoftObjectPath());
				}

				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorsForAssets(AssetPaths);
			});
		MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_EditAssetsLabel", "Edit..."),
				LOCTEXT("ContextMenu_EditAssets", "Opens the selected aset in the relevant editor."),
				FSlateIcon(),
				EditSelectedAssets,
				NAME_None,
				EUserInterfaceActionType::Button
			);

		//////////////////////////////////////////////////////////////////////////
		/// FindInContentBrowser

		FUIAction FindInContentBrowser;
		FindInContentBrowser.CanExecuteAction = HasSelectionAndRegistrySourceAndCanExecute;
		FindInContentBrowser.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().SyncBrowserToAssets(GetAssetDataForSelection());
		});
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_FindInContentBrowserLabel", "Find in Content Browser..."),
			LOCTEXT("ContextMenu_FindInContentBrowser", "Browses to the associated asset and selects it in the most recently used Content Browser (summoning one if necessary)"),
			FSlateIcon(),
			FindInContentBrowser,
			NAME_None,
			EUserInterfaceActionType::Button
		);

		//////////////////////////////////////////////////////////////////////////
		/// OpenReferenceViewer

		FUIAction OpenReferenceViewer;
		OpenReferenceViewer.CanExecuteAction = HasSelectionAndCanExecute;
		OpenReferenceViewer.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				TArray<FAssetIdentifier> AssetIdentifiers;
				IAssetManagerEditorModule::ExtractAssetIdentifiersFromAssetDataList(GetAssetDataForSelection(), AssetIdentifiers);

				if (AssetIdentifiers.Num() > 0)
				{
					IAssetManagerEditorModule::Get().OpenReferenceViewerUI(AssetIdentifiers);
				}
			});
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_OpenReferenceViewerLabel", "Reference Viewer..."),
			LOCTEXT("ContextMenu_OpenReferenceViewer", "Launches the reference viewer showing the selected assets' references"),
			FSlateIcon(),
			OpenReferenceViewer,
			NAME_None,
			EUserInterfaceActionType::Button
		);

		//////////////////////////////////////////////////////////////////////////
		/// ExportDependencies

		FUIAction ExportDependenciesAction;
		ExportDependenciesAction.CanExecuteAction = HasSelectionAndCanExecute;

		ExportDependenciesAction.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				ExportDependencyData();
			});

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ExportDependenciesLabel", "Export Dependencies..."),
			LOCTEXT("ContextMenu_ExportDependencies", "Export dependency CSVs for the selected asset"),
			FSlateIcon(),
			ExportDependenciesAction,
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
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
	int32 NewlySelectedAssetRowIndex = -1;
	TSet<int32> SelectionSetIndices;

	for (const UE::Insights::FTableTreeNodePtr& Node : SelectedNodes)
	{
		if (Node->Is<FAssetTreeNode>() && !Node->IsGroup())
		{
			NewSelectedAssetNode = StaticCastSharedPtr<FAssetTreeNode>(Node);
			NewlySelectedAssetRowIndex = NewSelectedAssetNode->GetRowIndex();
			SelectionSetIndices.Add(NewlySelectedAssetRowIndex);
			++NumSelectedAssets;
		}
		else if (Node->Is<UE::Insights::FCustomTableTreeNode>() && Node->IsGroup())
		{
			int32 RowIndex = Node->GetRowIndex();
			if (RowIndex != UE::Insights::FTableRowId::InvalidRowIndex)
			{
				SelectionSetIndices.Add(RowIndex);
				NewlySelectedAssetRowIndex = RowIndex;
				++NumSelectedAssets;
			}
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

		const FAssetTableRow& AssetTableRow = GetAssetTable()->GetAssetChecked(NewlySelectedAssetRowIndex);
		FooterCenterText1 = FText::FromString(AssetTableRow.GetPath());
		FooterCenterText2 = FText::FromString(AssetTableRow.GetName());
		FooterRightText1 = FText::Format(LOCTEXT("FooterRightFmt3", "Self: {0} Unique: {1} Shared: {2} External: {3}"),
			FText::AsMemory(AssetTableRow.GetStagedCompressedSize()),
			FText::AsMemory(AssetTableRow.GetOrComputeTotalSizeUniqueDependencies(*GetAssetTable(), NewlySelectedAssetRowIndex)),
			FText::AsMemory(AssetTableRow.GetOrComputeTotalSizeSharedDependencies(*GetAssetTable(), NewlySelectedAssetRowIndex)),
			FText::AsMemory(AssetTableRow.GetOrComputeTotalSizeExternalDependencies(*GetAssetTable(), NewlySelectedAssetRowIndex)));
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

		int64 TotalExternalDependencySize = FAssetTableRow::ComputeTotalSizeExternalDependencies(*GetAssetTable(), SelectionSetIndices);
		FAssetTableDependencySizes Sizes = FAssetTableRow::ComputeDependencySizes(*GetAssetTable(), SelectionSetIndices, nullptr, nullptr);
		FooterRightText1 = FText::Format(LOCTEXT("FooterRightFmt3", "Self: {0} Unique: {1} Shared: {2} External: {3}"),
			FText::AsMemory(TotalSelfSize),
			FText::AsMemory(Sizes.UniqueDependenciesSize),
			FText::AsMemory(Sizes.SharedDependenciesSize),
			FText::AsMemory(TotalExternalDependencySize));
	}

	if (NumSelectedAssets != 1)
	{
		NewSelectedAssetNode.Reset();
	}

	if (SelectedAssetNode != NewSelectedAssetNode)
	{
		SelectedAssetNode = NewSelectedAssetNode;
	}

	SelectedIndices = SelectionSetIndices;

	if (OnSelectionChanged.IsBound())
	{
		OnSelectionChanged.Execute(SelectedNodes);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
