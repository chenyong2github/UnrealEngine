// Copyright Epic Games, Inc. All Rights Reserved.

#include "STaskTableTreeView.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/TasksProfiler.h"
#include "Widgets/Input/SComboBox.h"

// Insights
#include "Insights/InsightsStyle.h"
#include "Insights/TaskGraphProfiler/TaskGraphProfilerManager.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskEntry.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskGraphRelation.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskNode.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTimingTrack.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/FilterConfigurator.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

#define LOCTEXT_NAMESPACE "STaskTableTreeView"

using namespace TraceServices;

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTaskTableTreeViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FTaskTableTreeViewCommands : public TCommands<FTaskTableTreeViewCommands>
{
public:
	FTaskTableTreeViewCommands()
	: TCommands<FTaskTableTreeViewCommands>(
		TEXT("TaskTableTreeViewCommands"),
		NSLOCTEXT("Contexts", "TaskTableTreeViewCommands", "Insights - Task Table Tree View"),
		NAME_None,
		FInsightsStyle::GetStyleSetName())
	{
	}

	virtual ~FTaskTableTreeViewCommands()
	{
	}

	// UI_COMMAND takes long for the compiler to optimize
	PRAGMA_DISABLE_OPTIMIZATION
	virtual void RegisterCommands() override
	{
		UI_COMMAND(Command_GoToTask, "Go To Task", "Pan and zoom to the task in Timing View.", EUserInterfaceActionType::Button, FInputChord());
	}
	PRAGMA_ENABLE_OPTIMIZATION

	TSharedPtr<FUICommandInfo> Command_GoToTask;
};

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

	AddCommmands();

	// Make sure the default value is applied.
	TimestampOptions_OnSelectionChanged(SelectedTimestampOption);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::ExtendMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("Node", LOCTEXT("ContextMenu_Section_Task", "Task"));

	{
		MenuBuilder.AddMenuEntry
		(
			FTaskTableTreeViewCommands::Get().Command_GoToTask,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.GoToTask")
		);
	}

	MenuBuilder.EndSection();
}

void STaskTableTreeView::AddCommmands()
{
	FTaskTableTreeViewCommands::Register();

	CommandList->MapAction(FTaskTableTreeViewCommands::Get().Command_GoToTask, FExecuteAction::CreateSP(this, &STaskTableTreeView::ContextMenu_GoToTask_Execute), FCanExecuteAction::CreateSP(this, &STaskTableTreeView::ContextMenu_GoToTask_CanExecute));
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
	double NewQueryStartTime = FTimingProfilerManager::Get()->GetSelectionStartTime();
	double NewQueryEndTime = FTimingProfilerManager::Get()->GetSelectionEndTime();

	if (NewQueryStartTime >= NewQueryEndTime)
	{
		return;
	}

	if (NewQueryStartTime != QueryStartTime || NewQueryEndTime != QueryEndTime)
	{
		QueryStartTime = NewQueryStartTime;
		QueryEndTime = NewQueryEndTime;
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
						FName NodeName(BaseNodeName, TaskInfo.Id + 1);
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
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Timestamps", "Timestamps"))
		]
		
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.MinDesiredWidth(160.0f)
			[
				SNew(SComboBox<TSharedPtr<ETimestampOptions>>)
				.OptionsSource(GetAvailableTimestampOptions())
				.OnSelectionChanged(this, &STaskTableTreeView::TimestampOptions_OnSelectionChanged)
				.OnGenerateWidget(this, &STaskTableTreeView::TimestampOptions_OnGenerateWidget)
				.IsEnabled(this, &STaskTableTreeView::TimestampOptions_IsEnabled)
				[
					SNew(STextBlock)
					.Text(this, &STaskTableTreeView::TimestampOptions_GetSelectionText)
				]
			]
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> STaskTableTreeView::ConstructFooter()
{
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STaskTableTreeView::TimestampOptions_OnGenerateWidget(TSharedPtr<ETimestampOptions> InOption)
{
	auto GetTooltipText = [](ETimestampOptions InOption)
	{
		switch (InOption)
		{
			case ETimestampOptions::Absolute:
			{
				return LOCTEXT("AbsoluteValueTooltip", "The timestamps for all columns will show absolute values.");
			}
			case ETimestampOptions::RelativeToPrevious:
			{
				return LOCTEXT("RelativeToPreviousTooltip", "The timestamps for all columns will show values relative to the previous stage. Ex: Scheduled will be relative to Launched.");
			}
			case ETimestampOptions::RelativeToCreated:
			{
				return LOCTEXT("RelativeToCreatedTooltip", "The timestamps for all columns will show values relative to the created time.");
			}
		}

		return FText();
	};

	TSharedRef<SHorizontalBox> Widget = SNew(SHorizontalBox);
	Widget->AddSlot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(TimestampOptions_GetText(*InOption))
			.ToolTipText(GetTooltipText(*InOption))
			.Margin(2.0f)
		];

	return Widget;
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

void STaskTableTreeView::InternalCreateGroupings()
{
	STableTreeView::InternalCreateGroupings();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<TSharedPtr<STaskTableTreeView::ETimestampOptions>>* STaskTableTreeView::GetAvailableTimestampOptions()
{
	if (AvailableTimestampOptions.Num() == 0)
	{
		AvailableTimestampOptions.Add(MakeShared<ETimestampOptions>(ETimestampOptions::Absolute));
		AvailableTimestampOptions.Add(MakeShared<ETimestampOptions>(ETimestampOptions::RelativeToPrevious));
		AvailableTimestampOptions.Add(MakeShared<ETimestampOptions>(ETimestampOptions::RelativeToCreated));
	}

	return &AvailableTimestampOptions;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::TimestampOptions_OnSelectionChanged(TSharedPtr<ETimestampOptions> InOption, ESelectInfo::Type SelectInfo)
{
	TimestampOptions_OnSelectionChanged(*InOption);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::TimestampOptions_OnSelectionChanged(ETimestampOptions InOption)
{
	SelectedTimestampOption = InOption;

	switch (SelectedTimestampOption)
	{
	case ETimestampOptions::Absolute:
	{
		GetTaskTable()->SwitchToAbsoluteTimestamps();
		break;
	}
	case ETimestampOptions::RelativeToPrevious:
	{
		GetTaskTable()->SwitchToRelativeToPreviousTimestamps();
		break;
	}
	case ETimestampOptions::RelativeToCreated:
	{
		GetTaskTable()->SwitchToRelativeToCreatedTimestamps();
		break;
	}
	}

	UpdateTree();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STaskTableTreeView::TimestampOptions_GetSelectionText() const
{
	return TimestampOptions_GetText(SelectedTimestampOption);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STaskTableTreeView::TimestampOptions_GetText(ETimestampOptions InOption) const
{
	switch (InOption)
	{
		case ETimestampOptions::Absolute:
		{
			return LOCTEXT("Absolute", "Absolute");
		}
		case ETimestampOptions::RelativeToPrevious:
		{
			return LOCTEXT("RelativeToPrevious", "Relative To Previous");
		}
		case ETimestampOptions::RelativeToCreated:
		{
			return LOCTEXT("RelativeToCreated", "Relative To Created");
		}
	}

	return FText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STaskTableTreeView::TimestampOptions_IsEnabled() const
{
	return !bIsUpdateRunning;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STaskTableTreeView::ContextMenu_GoToTask_CanExecute() const
{
	TArray<FTableTreeNodePtr> SelectedItems;
	TreeView->GetSelectedItems(SelectedItems);

	if (SelectedItems.Num() != 1)
	{
		return false;
	}

	FTaskNodePtr SelectedTask = StaticCastSharedPtr<FTaskNode>(SelectedItems[0]);

	if (!SelectedTask.IsValid() || SelectedTask->IsGroup())
	{
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::ContextMenu_GoToTask_Execute()
{
	TArray<FTableTreeNodePtr> SelectedItems;
	TreeView->GetSelectedItems(SelectedItems);

	if (SelectedItems.Num() != 1)
	{
		return;
	}

	FTaskNodePtr SelectedTask = StaticCastSharedPtr<FTaskNode>(SelectedItems[0]);
	const FTaskEntry* TaskEntry = SelectedTask.IsValid() ? SelectedTask->GetTask() : nullptr;

	if (TaskEntry == nullptr)
	{
		return;
	}

	TSharedPtr<STimingProfilerWindow> TimingWindow = FTimingProfilerManager::Get()->GetProfilerWindow();
	if (!TimingWindow.IsValid())
	{
		return;
	}

	TSharedPtr<STimingView> TimingView = TimingWindow->GetTimingView();
	if (!TimingView.IsValid())
	{
		return;
	}

	FTaskGraphProfilerManager::Get()->ShowTaskRelations(TaskEntry->GetId());

	double Duration = (TaskEntry->GetFinishedTimestamp() - TaskEntry->GetCreatedTimestamp()) * 1.5;
	TimingView->ZoomOnTimeInterval(TaskEntry->GetCreatedTimestamp() - Duration * 0.15, Duration);

	TSharedPtr<FTaskTimingSharedState> TaskSharedState = FTaskGraphProfilerManager::Get()->GetTaskTimingSharedState();

	if (TaskSharedState.IsValid() && FTaskGraphProfilerManager::Get()->GetShowAnyRelations())
	{
		TaskSharedState->SetTaskId(TaskEntry->GetId());
	}

	TSharedPtr<FThreadTimingSharedState> ThreadTimingState = TimingView->GetThreadTimingSharedState();
	if (!ThreadTimingState.IsValid())
	{
		return;
	}

	TSharedPtr<FCpuTimingTrack> Track = ThreadTimingState->GetCpuTrack(TaskEntry->GetStartedThreadId());
	if (!Track.IsValid())
	{
		return;
	}

	TimingView->SelectTimingTrack(Track, true);

	FTimingEventSearchParameters SearchParams(TaskEntry->StartedTimestamp, TaskEntry->FinishedTimestamp, ETimingEventSearchFlags::StopAtFirstMatch, 
		[TaskEntry](double StartTime, double EndTime, uint32 Depth)
		{
			if (StartTime >= TaskEntry->GetStartedTimestamp() && EndTime <= TaskEntry->GetFinishedTimestamp())
			{
				return true;
			}

			return false;
		});

	const TSharedPtr<const ITimingEvent> FoundEvent = Track->SearchEvent(SearchParams);
	TimingView->SelectTimingEvent(FoundEvent, true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STaskTableTreeView::TreeView_OnMouseButtonDoubleClick(FTableTreeNodePtr TreeNode)
{
	if (!TreeNode->IsGroup())
	{
		ContextMenu_GoToTask_Execute();
	}

	STableTreeView::TreeView_OnMouseButtonDoubleClick(TreeNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
