// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshPoseTrack.h"
#include "Insights/ITimingViewDrawHelper.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"

#if WITH_ENGINE
#include "Components/LineBatchComponent.h"
#endif

#define LOCTEXT_NAMESPACE "SkeletalMeshPoseTrack"

const FName FSkeletalMeshPoseTrack::TypeName(TEXT("Animation"));
const FName FSkeletalMeshPoseTrack::SubTypeName(TEXT("SkeletalMeshPose"));

FSkeletalMeshPoseTrack::FSkeletalMeshPoseTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, const TCHAR* InName)
	: TGameplayTrackMixin<FTimingEventsTrack>(InObjectID, FSkeletalMeshPoseTrack::TypeName, FSkeletalMeshPoseTrack::SubTypeName, FText::Format(LOCTEXT("SkelMeshPoseName", "{0} Pose"), FText::FromString(FString(InName))))
	, SharedData(InSharedData)
	, bDrawMarkerTime(false)
	, bDrawSelectedEvent(false)
	, bDrawHoveredEvent(false)
	, bDrawSelection(false)
{
}

void FSkeletalMeshPoseTrack::Reset()
{
	FTimingEventsTrack::Reset();

	SetHeight(16.0f);
}

void FSkeletalMeshPoseTrack::Draw( ITimingViewDrawHelper& Helper) const
{
	FSkeletalMeshPoseTrack& Track = *const_cast<FSkeletalMeshPoseTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

		if(AnimationProvider)
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			AnimationProvider->ReadSkeletalMeshPoseTimeline(GetGameplayTrack().GetObjectId(), [&Helper](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
			{
				auto DrawEvent = [&Helper](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
				{
					static TCHAR Buffer[256];
					FCString::Snprintf(Buffer, 256, TEXT("%d Bones"), InMessage.NumTransforms);
					Helper.AddEvent(InStartTime, InEndTime, 0, Buffer);
				};

				if (FTimingEventsTrack::bUseDownSampling)
				{
					const double SecondsPerPixel = 1.0 / Helper.GetViewport().GetScaleX();
					InTimeline.EnumerateEventsDownSampled(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), SecondsPerPixel, DrawEvent);
				}
				else
				{
					InTimeline.EnumerateEvents(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), DrawEvent);
				}
			});
		}

		Helper.EndTimeline(Track);
	}
}

void FSkeletalMeshPoseTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	Tooltip.ResetContent();

	Tooltip.AddTitle(LOCTEXT("SkeletalMeshPoseTooltipTitle", "Skeletal Mesh Pose").ToString());

	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadSkeletalMeshPoseMessage(GetGameplayTrack().GetObjectId(), HoveredTimingEvent.TypeId, [&Tooltip, &HoveredTimingEvent](const FSkeletalMeshPoseMessage& InMessage)
		{
			Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(HoveredTimingEvent.StartTime).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("BoneCount", "Bone Count").ToString(), FText::AsNumber(InMessage.NumTransforms).ToString());
		});
	}

	Tooltip.UpdateLayout();
}

bool FSkeletalMeshPoseTrack::SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const
{
	struct FSearchTimingEventContext
	{
		const double StartTime;
		const double EndTime;
		TFunctionRef<bool(double, double, uint32)> Predicate;
		FTimingEvent& TimingEvent;
		const bool bStopAtFirstMatch;
		const bool bSearchForLargestEvent;
		mutable bool bFound;
		mutable bool bContinueSearching;
		mutable double LargestDuration;

		FSearchTimingEventContext(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent)
			: StartTime(InStartTime)
			, EndTime(InEndTime)
			, Predicate(InPredicate)
			, TimingEvent(InOutTimingEvent)
			, bStopAtFirstMatch(bInStopAtFirstMatch)
			, bSearchForLargestEvent(bInSearchForLargestEvent)
			, bFound(false)
			, bContinueSearching(true)
			, LargestDuration(-1.0)
		{
		}

		void CheckMessage(double EventStartTime, double EventEndTime, uint32 EventDepth, uint64 InMessageId)
		{
			if (bContinueSearching && Predicate(EventStartTime, EventEndTime, EventDepth))
			{
				if (!bSearchForLargestEvent || EventEndTime - EventStartTime > LargestDuration)
				{
					LargestDuration = EventEndTime - EventStartTime;

					TimingEvent.TypeId = InMessageId;
					TimingEvent.Depth = EventDepth;
					TimingEvent.StartTime = EventStartTime;
					TimingEvent.EndTime = EventEndTime;

					bFound = true;
					bContinueSearching = !bStopAtFirstMatch || bSearchForLargestEvent;
				}
			}
		}
	};

	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		FSearchTimingEventContext Context(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadSkeletalMeshPoseTimeline(GetGameplayTrack().GetObjectId(), [&Context, &InStartTime, &InEndTime](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InStartTime, InEndTime, [&Context](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
			{
				Context.CheckMessage(InEventStartTime, InEventEndTime, 0, InMessage.MessageId);
			});
		});
	}

	return false;
}

void FSkeletalMeshPoseTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection(TEXT("ShowPoseSection"), LOCTEXT("ShowPose", "Show Pose"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleDrawMarkerTime", "Marker Time"),
			LOCTEXT("ToggleDrawMarkerTime_Tooltip", "Draw the pose at the current marker time"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ bDrawMarkerTime = !bDrawMarkerTime; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return bDrawMarkerTime; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleDrawSelection", "Selection"),
			LOCTEXT("ToggleDrawSelection_Tooltip", "Draw the pose for the currently selected event"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ bDrawSelectedEvent = !bDrawSelectedEvent; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return bDrawSelectedEvent; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleDrawHovered", "Hovered"),
			LOCTEXT("ToggleDrawSelection_Tooltip", "Draw the pose for the currently hovered event"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ bDrawHoveredEvent = !bDrawHoveredEvent; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return bDrawHoveredEvent; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleDrawSelectedRange", "Selected Range"),
			LOCTEXT("ToggleDrawSelectedRange_Tooltip", "Draw poses for the currently selected range"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ bDrawSelection = !bDrawSelection; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return bDrawSelection; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();
}

#if WITH_ENGINE

void FSkeletalMeshPoseTrack::DrawPoses(ULineBatchComponent* InLineBatcher, double SelectionStartTime, double SelectionEndTime)
{
	if(SharedData.IsAnalysisSessionValid())
	{
		const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

		if(AnimationProvider)
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			AnimationProvider->ReadSkeletalMeshPoseTimeline(GetGameplayTrack().GetObjectId(), [this, &InLineBatcher, &AnimationProvider, &SelectionStartTime, &SelectionEndTime](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
			{
				static TArray<FBatchedLine> Lines;

				InTimeline.EnumerateEvents(SelectionStartTime, SelectionEndTime, [&InLineBatcher, &AnimationProvider, &SelectionStartTime, &SelectionEndTime](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
				{
					if(SelectionStartTime == SelectionEndTime || (InStartTime >= SelectionStartTime && InEndTime <= SelectionEndTime))
					{
						const FSkeletalMeshInfo* SkeletalMeshInfo = AnimationProvider->FindSkeletalMeshInfo(InMessage.MeshId);
						if(SkeletalMeshInfo)
						{
							AnimationProvider->EnumerateSkeletalMeshPose(InMessage, *SkeletalMeshInfo, [&InLineBatcher](const FTransform& InTransform, const FTransform& InParentTransform)
							{
								Lines.Emplace(InParentTransform.GetLocation(), InTransform.GetLocation(), FLinearColor::Red, 1.0f, 0.2f, SDPG_Foreground);
							});
						}
					}
				});

				InLineBatcher->DrawLines(Lines);

				Lines.Reset();
			});
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE