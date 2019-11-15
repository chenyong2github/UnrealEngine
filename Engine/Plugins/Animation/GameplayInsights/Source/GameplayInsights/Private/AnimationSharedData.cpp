// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationSharedData.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplaySharedData.h"
#include "ObjectEventsTrack.h"
#include "SkeletalMeshPoseTrack.h"
#include "AnimationTickRecordsTrack.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "AnimationProvider.h"
#include "GameplayProvider.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "AnimationSharedData"

FAnimationSharedData::FAnimationSharedData(FGameplaySharedData& InGameplaySharedData)
	: GameplaySharedData(InGameplaySharedData)
	, AnalysisSession(nullptr)
	, SelectedEventStartTime(0.0)
	, SelectedEventEndTime(0.0)
	, HoveredEventStartTime(0.0)
	, HoveredEventEndTime(0.0)
	, SelectionStartTime(0.0)
	, SelectionEndTime(0.0)
	, MarkerTime(0.0)
	, bSelectedEventValid(false)
	, bHoveredEventValid(false)
	, bSelectionValid(false)
	, bTimeMarkerValid(false)
	, bAnimationTracksEnabled(true)
{
}

void FAnimationSharedData::OnBeginSession(Insights::ITimingViewSession& InTimingViewSession)
{
	SkeletalMeshPoseTracks.Reset();
	AnimationTickRecordsTracks.Reset();

	SelectedEventChangedHandle = InTimingViewSession.OnSelectedEventChanged().AddRaw(this, &FAnimationSharedData::OnSelectedEventChanged);
	HoveredEventChangedHandle = InTimingViewSession.OnHoveredEventChanged().AddRaw(this, &FAnimationSharedData::OnHoveredEventChanged);
	TimeMarkerChangedHandle = InTimingViewSession.OnTimeMarkerChanged().AddRaw(this, &FAnimationSharedData::OnTimeMarkerChanged);
	SelectionChangedHandle = InTimingViewSession.OnSelectionChanged().AddRaw(this, &FAnimationSharedData::OnSelectionChanged);
}

void FAnimationSharedData::OnEndSession(Insights::ITimingViewSession& InTimingViewSession)
{
	SkeletalMeshPoseTracks.Reset();
	AnimationTickRecordsTracks.Reset();

	InTimingViewSession.OnSelectedEventChanged().Remove(SelectedEventChangedHandle);
	InTimingViewSession.OnHoveredEventChanged().Remove(HoveredEventChangedHandle);
	InTimingViewSession.OnTimeMarkerChanged().Remove(TimeMarkerChangedHandle);
	InTimingViewSession.OnSelectionChanged().Remove(SelectionChangedHandle);
}

void FAnimationSharedData::Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	AnalysisSession = &InAnalysisSession;

	const FAnimationProvider* AnimationProvider = InAnalysisSession.ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
	const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(AnimationProvider && GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(GetAnalysisSession());

		// Add tracks for each tracked object's animation data
		GameplayProvider->EnumerateObjects([this, &InTimingViewSession, &InAnalysisSession, &AnimationProvider, &GameplayProvider](const FObjectInfo& InObjectInfo)
		{
			TSharedRef<FObjectEventsTrack> ObjectEventsTrack = GameplaySharedData.GetObjectEventsTrackForId(InTimingViewSession, InAnalysisSession, InObjectInfo);

			AnimationProvider->ReadSkeletalMeshPoseTimeline(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession](const IAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
			{
				ObjectEventsTrack->SetVisibilityFlag(GameplaySharedData.AreGameplayTracksEnabled());

				auto FindSkeletalMeshPoseTrack = [](const FBaseTimingTrack& InTrack)
				{
					if (InTrack.GetType() == FSkeletalMeshPoseTrack::TypeName &&
						InTrack.GetSubType() == FSkeletalMeshPoseTrack::SubTypeName)
					{
						return true;
					}

					return false;
				};

				TSharedPtr<FSkeletalMeshPoseTrack> SkeletalMeshPoseTrack = StaticCastSharedPtr<FSkeletalMeshPoseTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindSkeletalMeshPoseTrack));
				if(!SkeletalMeshPoseTrack.IsValid())
				{
					SkeletalMeshPoseTrack = MakeShared<FSkeletalMeshPoseTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
					SkeletalMeshPoseTrack->SetVisibilityFlag(bAnimationTracksEnabled);
					SkeletalMeshPoseTracks.Add(SkeletalMeshPoseTrack.ToSharedRef());

					InTimingViewSession.AddScrollableTrack(SkeletalMeshPoseTrack);
					GameplaySharedData.InvalidateObjectTracksOrder();

					ObjectEventsTrack->GetGameplayTrack().AddChildTrack(SkeletalMeshPoseTrack->GetGameplayTrack());
				}
			});

			AnimationProvider->EnumerateTickRecordTimelines(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession, &GameplayProvider](uint64 InAssetId, const IAnimationProvider::TickRecordTimeline& InTimeline)
			{
				ObjectEventsTrack->SetVisibilityFlag(GameplaySharedData.AreGameplayTracksEnabled());

				auto FindTickRecordTrackWithAssetId = [&InAssetId](const FBaseTimingTrack& InTrack)
				{
					if (InTrack.GetType() == FAnimationTickRecordsTrack::TypeName &&
						InTrack.GetSubType() == FAnimationTickRecordsTrack::SubTypeName)
					{
						const FAnimationTickRecordsTrack& AnimationTickRecordsTrack = *static_cast<const FAnimationTickRecordsTrack*>(&InTrack);
						return AnimationTickRecordsTrack.GetAssetId() == InAssetId;
					}

					return false;
				};

				TSharedPtr<FAnimationTickRecordsTrack> AnimationTickRecordsTrack = StaticCastSharedPtr<FAnimationTickRecordsTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindTickRecordTrackWithAssetId));
				if(!AnimationTickRecordsTrack.IsValid())
				{
					const FObjectInfo* AssetObjectInfo = GameplayProvider->FindObjectInfo(InAssetId);
					FString AssetName = AssetObjectInfo ? AssetObjectInfo->Name : LOCTEXT("UnknownAsset", "Unknown").ToString();
					AnimationTickRecordsTrack = MakeShared<FAnimationTickRecordsTrack>(*this, InObjectInfo.Id, InAssetId, *AssetName);
					AnimationTickRecordsTrack->SetVisibilityFlag(bAnimationTracksEnabled);
					AnimationTickRecordsTracks.Add(AnimationTickRecordsTrack.ToSharedRef());

					InTimingViewSession.AddScrollableTrack(AnimationTickRecordsTrack);
					GameplaySharedData.InvalidateObjectTracksOrder();

					ObjectEventsTrack->GetGameplayTrack().AddChildTrack(AnimationTickRecordsTrack->GetGameplayTrack());
				}
			});
		});
	}
}

void FAnimationSharedData::ExtendFilterMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleAnimationTracks", "Animation Tracks"),
		LOCTEXT("ToggleAnimationTracks_Tooltip", "Show/hide the animation tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FAnimationSharedData::ToggleAnimationTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FAnimationSharedData::AreAnimationTracksEnabled)),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);
}

void FAnimationSharedData::ToggleAnimationTracks()
{
	bAnimationTracksEnabled = !bAnimationTracksEnabled;

	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		PoseTrack->SetVisibilityFlag(bAnimationTracksEnabled);
	}

	for(TSharedRef<FAnimationTickRecordsTrack> TickRecordTrack : AnimationTickRecordsTracks)
	{
		TickRecordTrack->SetVisibilityFlag(bAnimationTracksEnabled);
	}
}

bool FAnimationSharedData::AreAnimationTracksEnabled() const
{
	return bAnimationTracksEnabled;
}

void FAnimationSharedData::OnSelectedEventChanged(const TSharedPtr<const ITimingEvent> InEvent)
{
	bSelectedEventValid = InEvent.IsValid();
	if(bSelectedEventValid)
	{
		SelectedEventTrack = InEvent->GetTrack();
		SelectedEventStartTime = InEvent->GetStartTime();
		SelectedEventEndTime = InEvent->GetEndTime();
	}
	else
	{
		SelectedEventTrack = nullptr;
		SelectedEventStartTime = 0.0;
		SelectedEventEndTime = 0.0;
	}
}

void FAnimationSharedData::OnHoveredEventChanged(const TSharedPtr<const ITimingEvent> InEvent)
{
	bHoveredEventValid = InEvent.IsValid();
	if(bHoveredEventValid)
	{
		HoveredEventTrack = InEvent->GetTrack();
		HoveredEventStartTime = InEvent->GetStartTime();
		HoveredEventEndTime = InEvent->GetEndTime();
	}
	else
	{
		HoveredEventTrack = nullptr;
		HoveredEventStartTime = 0.0;
		HoveredEventEndTime = 0.0;
	}
}

void FAnimationSharedData::OnTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker)
{
	bTimeMarkerValid = InTimeMarker != std::numeric_limits<double>::infinity();
	MarkerTime = InTimeMarker;
}

void FAnimationSharedData::OnSelectionChanged(Insights::ETimeChangedFlags InFlags, double InStartTime, double InEndTime)
{
	bSelectionValid = InStartTime < InEndTime;
	SelectionStartTime = InStartTime;
	SelectionEndTime = InEndTime;
}

#if WITH_ENGINE

void FAnimationSharedData::DrawPoses(ULineBatchComponent* InLineBatcher)
{
	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		if(PoseTrack->IsVisible())
		{
			if(bSelectedEventValid && PoseTrack == SelectedEventTrack && PoseTrack->ShouldDrawSelectedEvent())
			{
				PoseTrack->DrawPoses(InLineBatcher, SelectedEventStartTime, SelectedEventEndTime);
			}
		
			if(bHoveredEventValid && PoseTrack == HoveredEventTrack && PoseTrack->ShouldDrawHoveredEvent())
			{
				PoseTrack->DrawPoses(InLineBatcher, HoveredEventStartTime, HoveredEventEndTime);
			}

			if(bTimeMarkerValid && PoseTrack->ShouldDrawMarkerTime())
			{
				PoseTrack->DrawPoses(InLineBatcher, MarkerTime, MarkerTime);
			}

			if(bSelectionValid && PoseTrack->ShouldDrawSelection())
			{
				PoseTrack->DrawPoses(InLineBatcher, SelectionStartTime, SelectionEndTime);
			}
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE