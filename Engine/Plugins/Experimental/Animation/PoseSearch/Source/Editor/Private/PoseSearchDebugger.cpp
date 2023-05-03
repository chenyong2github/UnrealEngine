// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDebugger.h"
#include "IAnimationProvider.h"
#include "IGameplayProvider.h"
#include "IRewindDebugger.h"
#include "PoseSearchDebuggerView.h"
#include "PoseSearchDebuggerViewModel.h"
#include "Styling/SlateIconFinder.h"
#include "Trace/PoseSearchTraceProvider.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "PoseSearchDebugger"

namespace UE::PoseSearch
{

FDebugger* FDebugger::Debugger;
void FDebugger::Initialize()
{
	Debugger = new FDebugger;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, Debugger);
}

void FDebugger::Shutdown()
{
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, Debugger);
	delete Debugger;
}

void FDebugger::RecordingStarted(IRewindDebugger*)
{
	UE::Trace::ToggleChannel(TEXT("PoseSearch"), true);
}

void FDebugger::RecordingStopped(IRewindDebugger*)
{
	UE::Trace::ToggleChannel(TEXT("PoseSearch"), false);
}

bool FDebugger::IsPIESimulating()
{
	return Debugger->RewindDebugger->IsPIESimulating();
}

bool FDebugger::IsRecording()
{
	return Debugger->RewindDebugger->IsRecording();

}

double FDebugger::GetRecordingDuration()
{
	return Debugger->RewindDebugger->GetRecordingDuration();
}

UWorld* FDebugger::GetWorld()
{
	return Debugger->RewindDebugger->GetWorldToVisualize();
}

const IRewindDebugger* FDebugger::GetRewindDebugger()
{
	return Debugger->RewindDebugger;
}

void FDebugger::Update(float DeltaTime, IRewindDebugger* InRewindDebugger)
{
	// Update active rewind debugger in use
	RewindDebugger = InRewindDebugger;
}

void FDebugger::OnViewClosed(uint64 InAnimInstanceId)
{
	TArray<TSharedRef<FDebuggerViewModel>>& Models = Debugger->ViewModels;
	for (int i = 0; i < Models.Num(); ++i)
	{
		if (Models[i]->AnimInstanceId == InAnimInstanceId)
		{
			Models.RemoveAtSwap(i);
			return;
		}
	}
	// Should always be a valid remove
	checkNoEntry();
}

TSharedPtr<FDebuggerViewModel> FDebugger::GetViewModel(uint64 InAnimInstanceId)
{
	TArray<TSharedRef<FDebuggerViewModel>>& Models = Debugger->ViewModels;
	for (int i = 0; i < Models.Num(); ++i)
	{
		if (Models[i]->AnimInstanceId == InAnimInstanceId)
		{
			return Models[i];
		}
	}
	return nullptr;
}

TSharedPtr<SDebuggerView> FDebugger::GenerateInstance(uint64 InAnimInstanceId)
{
	ViewModels.Add_GetRef(MakeShared<FDebuggerViewModel>(InAnimInstanceId))->RewindDebugger.BindStatic(&FDebugger::GetRewindDebugger);

	TSharedPtr<SDebuggerView> DebuggerView;

	SAssignNew(DebuggerView, SDebuggerView, InAnimInstanceId)
		.ViewModel_Static(&FDebugger::GetViewModel, InAnimInstanceId)
		.OnViewClosed_Static(&FDebugger::OnViewClosed);

	return DebuggerView;
}

FDebuggerTrack::FDebuggerTrack(uint64 InObjectId)
: ObjectId(InObjectId)
{
	BestCostData = MakeShared<SCurveTimelineView::FTimelineCurveData>();
	BruteForceCostData = MakeShared<SCurveTimelineView::FTimelineCurveData>();

	BestCostView = SNew(SCurveTimelineView)
		.CurveColor(FLinearColor::White)
		.ViewRange_Lambda([]()
		{
			return IRewindDebugger::Instance()->GetCurrentViewRange();
		})
		.RenderFill(false)
		.CurveData_Lambda([this]()
		{
			return BestCostData;
		});
	
	BruteForceCostView = SNew(SCurveTimelineView)
		.CurveColor(FLinearColor::Red)
		.ViewRange_Lambda([]()
		{
			return IRewindDebugger::Instance()->GetCurrentViewRange();
		})
		.RenderFill(false)
		.CurveData_Lambda([this]()
		{
			return BruteForceCostData;
		});

	OverlayView = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			BruteForceCostView.ToSharedRef()
		]
		+ SOverlay::Slot()
		[
			BestCostView.ToSharedRef()
		];
}

FSlateIcon FDebuggerTrack::GetIconInternal()
{
#if WITH_EDITOR
	return FSlateIconFinder::FindIconForClass(UAnimInstance::StaticClass());
#else
	return FSlateIcon();
#endif
}

bool FDebuggerTrack::UpdateInternal()
{
	IRewindDebugger* RewindDebugger = IRewindDebugger::Instance();
	
	const TraceServices::IAnalysisSession* AnalysisSession = RewindDebugger->GetAnalysisSession();
	check(AnalysisSession);
	if (const FTraceProvider* PoseSearchProvider = AnalysisSession->ReadProvider<FTraceProvider>(FTraceProvider::ProviderName))
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		TArray<SCurveTimelineView::FTimelineCurveData::CurvePoint>& BestCostPoints = BestCostData->Points;
		BestCostPoints.Reset();

		TArray<SCurveTimelineView::FTimelineCurveData::CurvePoint>& BruteForceCostPoints = BruteForceCostData->Points;
		BruteForceCostPoints.Reset();

		// convert time range to from rewind debugger times to profiler times
		TRange<double> TraceTimeRange = RewindDebugger->GetCurrentTraceRange();
		double StartTime = TraceTimeRange.GetLowerBoundValue();
		double EndTime = TraceTimeRange.GetUpperBoundValue();

		PoseSearchProvider->EnumerateMotionMatchingStateTimelines(ObjectId, [StartTime, EndTime, &BestCostPoints, &BruteForceCostPoints](const FTraceProvider::FMotionMatchingStateTimeline& InTimeline)
		{
			// this isn't very efficient, and it gets called every frame.  will need optimizing
			InTimeline.EnumerateEvents(StartTime, EndTime, [StartTime, EndTime, &BestCostPoints, &BruteForceCostPoints](double InStartTime, double InEndTime, uint32 InDepth, const FTraceMotionMatchingStateMessage& InMessage)
			{
				if (InEndTime > StartTime && InStartTime < EndTime)
				{
					BestCostPoints.Add({ InMessage.RecordingTime, InMessage.SearchBestCost });
					BruteForceCostPoints.Add({ InMessage.RecordingTime, InMessage.SearchBruteForceCost });
				}
				return TraceServices::EEventEnumerate::Continue;
			});
		});

		float MinValue = UE_MAX_FLT;
		float MaxValue = -UE_MAX_FLT;
		for (const SCurveTimelineView::FTimelineCurveData::CurvePoint& CurvePoint : BestCostPoints)
		{
			MinValue = FMath::Min(MinValue, CurvePoint.Value);
			MaxValue = FMath::Max(MaxValue, CurvePoint.Value);
		}
		for (const SCurveTimelineView::FTimelineCurveData::CurvePoint& CurvePoint : BruteForceCostPoints)
		{
			MinValue = FMath::Min(MinValue, CurvePoint.Value);
			MaxValue = FMath::Max(MaxValue, CurvePoint.Value);
		}

		BestCostView->SetFixedRange(MinValue, MaxValue);
		BruteForceCostView->SetFixedRange(MinValue, MaxValue);
	}

	if (TSharedPtr<IRewindDebuggerView> PinnedView = View.Pin())
	{
		PinnedView->SetTimeMarker(RewindDebugger->CurrentTraceTime());
	}

	return false;
}

FName FDebuggerTrack::GetNameInternal() const
{
	static const FName Name("PoseSearchDebugger");
	return Name;
}

FText FDebuggerTrack::GetDisplayNameInternal() const
{
	return LOCTEXT("PoseSearchDebuggerTabTitle", "Pose Search");
}

TSharedPtr<SWidget> FDebuggerTrack::GetTimelineViewInternal()
{
	return OverlayView;
}

TSharedPtr<SWidget> FDebuggerTrack::GetDetailsViewInternal()
{
	TSharedPtr<IRewindDebuggerView> RewindDebuggerView = FDebugger::Get()->GenerateInstance(ObjectId);
	View = RewindDebuggerView;
	return RewindDebuggerView;
}

// FDebuggerTrackCreator
///////////////////////////////////////////////////

FName FDebuggerTrackCreator::GetTargetTypeNameInternal() const
{
	static FName TargetTypeName = "AnimInstance";
	return TargetTypeName;
}

FName FDebuggerTrackCreator::GetNameInternal() const
{
	static const FName Name("PoseSearchDebugger");
	return Name;
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FDebuggerTrackCreator::CreateTrackInternal(uint64 ObjectId) const
{
	return MakeShared<FDebuggerTrack>(ObjectId);
}

bool FDebuggerTrackCreator::HasDebugInfoInternal(uint64 ObjectId) const
{
	// Get provider and validate
	const TraceServices::IAnalysisSession* Session = IRewindDebugger::Instance()->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

	const FTraceProvider* PoseSearchProvider = Session->ReadProvider<FTraceProvider>(FTraceProvider::ProviderName);
	const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider");
	const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
	if (!(PoseSearchProvider && AnimationProvider && GameplayProvider))
	{
		return false;
	}

	bool bHasData = false;

	PoseSearchProvider->EnumerateMotionMatchingStateTimelines(ObjectId, [&bHasData](const FTraceProvider::FMotionMatchingStateTimeline& InTimeline)
	{
		bHasData = true;
	});

	return bHasData;
}


} // namespace UE::PoseSearch

#undef LOCTEXT_NAMESPACE
