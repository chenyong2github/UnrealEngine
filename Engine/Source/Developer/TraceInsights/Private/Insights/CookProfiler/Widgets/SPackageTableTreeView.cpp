// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPackageTableTreeView.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/InsightsStyle.h"
#include "Insights/CookProfiler/CookProfilerManager.h"
#include "Insights/CookProfiler/ViewModels/PackageEntry.h"
#include "Insights/CookProfiler/ViewModels/PackageNode.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/FilterConfigurator.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

#define LOCTEXT_NAMESPACE "SPackageTableTreeView"

using namespace TraceServices;

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTaskTableTreeViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FPackageTableTreeViewCommands : public TCommands<FPackageTableTreeViewCommands>
{
public:
	FPackageTableTreeViewCommands()
	: TCommands<FPackageTableTreeViewCommands>(
		TEXT("PackageTableTreeViewCommands"),
		NSLOCTEXT("Contexts", "PackageTableTreeViewCommands", "Insights - Package Table Tree View"),
		NAME_None,
		FInsightsStyle::GetStyleSetName())
	{
	}

	virtual ~FPackageTableTreeViewCommands()
	{
	}

	// UI_COMMAND takes long for the compiler to optimize
	PRAGMA_DISABLE_OPTIMIZATION
	virtual void RegisterCommands() override
	{
	}
	PRAGMA_ENABLE_OPTIMIZATION
};

////////////////////////////////////////////////////////////////////////////////////////////////////

SPackageTableTreeView::SPackageTableTreeView()
{
	bRunInAsyncMode = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SPackageTableTreeView::~SPackageTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::Construct(const FArguments& InArgs, TSharedPtr<FPackageTable> InTablePtr)
{
	ConstructWidget(InTablePtr);

	AddCommmands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::ExtendMenu(FMenuBuilder& MenuBuilder)
{
}

void SPackageTableTreeView::AddCommmands()
{
	FPackageTableTreeViewCommands::Register();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::Reset()
{
	STableTreeView::Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	STableTreeView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!bIsUpdateRunning)
	{
		RebuildTree(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::RebuildTree(bool bResync)
{
	if (bDataLoaded == true)
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	if (!Session->IsAnalysisComplete())
	{
		return;
	}

	TSharedPtr<FPackageTable> PackageTable = GetPackageTable();
	TArray<FPackageEntry>& Packages = PackageTable->GetPackageEntries();
	Packages.Empty();
	TableTreeNodes.Empty();

	const TraceServices::ICookProfilerProvider* CookProvider = TraceServices::ReadCookProfilerProvider(*Session.Get());

	if (CookProvider)
	{
		TraceServices::FProviderReadScopeLock ProviderReadScope(*CookProvider);
		TArray<FTableTreeNodePtr>* Nodes = &TableTreeNodes;

		CookProvider->EnumeratePackages(0, 0, [&Packages, &PackageTable, Nodes](const TraceServices::FPackageData& Package)
			{
				Packages.Emplace(Package);
				uint32 Index = Packages.Num() - 1;
				FName NodeName(Package.Name);
				FPackageNodePtr NodePtr = MakeShared<FPackageNode>(NodeName, PackageTable, Index);
				Nodes->Add(NodePtr);
				return true;
			});
	}

	bDataLoaded = true;

	UpdateTree();
	TreeView->RebuildList();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SPackageTableTreeView::IsRunning() const
{
	return STableTreeView::IsRunning();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double SPackageTableTreeView::GetAllOperationsDuration()
{
	return STableTreeView::GetAllOperationsDuration();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SPackageTableTreeView::GetCurrentOperationName() const
{
	return STableTreeView::GetCurrentOperationName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> SPackageTableTreeView::ConstructToolbar()
{
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> SPackageTableTreeView::ConstructFooter()
{
	return nullptr;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::ApplyColumnConfig(const TArrayView<FColumnConfig>& Preset)
{
	for (const TSharedRef<FTableColumn>& ColumnRef : Table->GetColumns())
	{
		FTableColumn& Column = ColumnRef.Get();
		for (const FColumnConfig& Config : Preset)
		{
			if (Column.GetId() == Config.ColumnId)
			{
				if (Config.bIsVisible)
				{
					ShowColumn(Column);
					if (Config.Width > 0.0f)
					{
						TreeViewHeaderRow->SetColumnWidth(Column.GetId(), Config.Width);
					}
				}
				else
				{
					HideColumn(Column);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::InternalCreateGroupings()
{
	STableTreeView::InternalCreateGroupings();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPackageTableTreeView::TreeView_OnMouseButtonDoubleClick(FTableTreeNodePtr TreeNode)
{
	STableTreeView::TreeView_OnMouseButtonDoubleClick(TreeNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
