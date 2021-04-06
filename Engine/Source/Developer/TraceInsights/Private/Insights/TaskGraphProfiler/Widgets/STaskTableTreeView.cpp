// Copyright Epic Games, Inc. All Rights Reserved.

#include "STaskTableTreeView.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/TasksProfiler.h"

// Insights
#include "Insights/TaskGraphProfiler/TaskGraphProfilerManager.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskEntry.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskNode.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/FilterConfigurator.h"

#include <limits>

#define LOCTEXT_NAMESPACE "STaskTableTreeView"

using namespace TraceServices;

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

STaskTableTreeView::STaskTableTreeView()
{
	bRunInAsyncMode = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STaskTableTreeView::~STaskTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::Construct(const FArguments& InArgs, TSharedPtr<Insights::FTaskTable> InTablePtr)
{
	ConstructWidget(InTablePtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::Reset()
{
	STableTreeView::Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	STableTreeView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!bIsUpdateRunning)
	{
		RebuildTree(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::RebuildTree(bool bResync)
{
	if (FTimingProfilerManager::Get()->GetSelectionStartTime() != QueryStartTime ||
		FTimingProfilerManager::Get()->GetSelectionEndTime() != QueryEndTime)
	{
		QueryStartTime = FTimingProfilerManager::Get()->GetSelectionStartTime();
		QueryEndTime = FTimingProfilerManager::Get()->GetSelectionEndTime();
		TSharedPtr<FTaskTable> TaskTable = GetTaskTable();
		TArray<FTaskEntry>& Allocs = TaskTable->GetTaskEntries();
		Allocs.Empty();
		TableTreeNodes.Empty();

		if (QueryStartTime < QueryEndTime && Session.IsValid())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

			if (TasksProvider)
			{
				FName BaseNodeName(TEXT("task"));

				TArray<FTableTreeNodePtr>* Nodes = &TableTreeNodes;

				TasksProvider->EnumerateTasks(QueryStartTime, QueryEndTime, [&Allocs, &TaskTable, &BaseNodeName, Nodes](const TraceServices::FTaskInfo& TaskInfo)
					{
						Allocs.Emplace(TaskInfo);
						uint32 Index = Allocs.Num() - 1;
						FName NodeName(BaseNodeName, Index);
						FTaskNodePtr NodePtr = MakeShared<FTaskNode>(NodeName, TaskTable, Index);
						Nodes->Add(NodePtr);
						return TraceServices::ETaskEnumerationResult::Continue;
					});
			}
		}

		UpdateTree();
		TreeView->RebuildList();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STaskTableTreeView::IsRunning() const
{
	return STableTreeView::IsRunning();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double STaskTableTreeView::GetAllOperationsDuration()
{
	return STableTreeView::GetAllOperationsDuration();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STaskTableTreeView::GetCurrentOperationName() const
{
	return STableTreeView::GetCurrentOperationName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> STaskTableTreeView::ConstructToolbar()
{
	return nullptr;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::ApplyColumnConfig(const TArrayView<FColumnConfig>& Preset)
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

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> STaskTableTreeView::ConstructFooter()
{
	return nullptr;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::InternalCreateGroupings()
{
	STableTreeView::InternalCreateGroupings();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
