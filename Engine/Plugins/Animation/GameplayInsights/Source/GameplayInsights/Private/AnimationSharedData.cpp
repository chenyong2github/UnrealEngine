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
#include "AnimNodesTrack.h"
#include "GameplayTimingViewExtender.h"

#if WITH_EDITOR
#include "EditorViewportClient.h"
#include "Editor/EditorEngine.h"
#endif

#define LOCTEXT_NAMESPACE "AnimationSharedData"

FAnimationSharedData::FAnimationSharedData(FGameplaySharedData& InGameplaySharedData)
	: GameplaySharedData(InGameplaySharedData)
	, AnalysisSession(nullptr)
	, MarkerTime(0.0)
	, bTimeMarkerValid(false)
	, bAnimationTracksEnabled(true)
{
}

void FAnimationSharedData::OnBeginSession(Insights::ITimingViewSession& InTimingViewSession)
{
	SkeletalMeshPoseTracks.Reset();
	AnimationTickRecordsTracks.Reset();

	TimeMarkerChangedHandle = InTimingViewSession.OnTimeMarkerChanged().AddRaw(this, &FAnimationSharedData::OnTimeMarkerChanged);
}

void FAnimationSharedData::OnEndSession(Insights::ITimingViewSession& InTimingViewSession)
{
	SkeletalMeshPoseTracks.Reset();
	AnimationTickRecordsTracks.Reset();

	InTimingViewSession.OnTimeMarkerChanged().Remove(TimeMarkerChangedHandle);
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

				TSharedPtr<FSkeletalMeshPoseTrack> ExistingSkeletalMeshPoseTrack = StaticCastSharedPtr<FSkeletalMeshPoseTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindSkeletalMeshPoseTrack));
				if(!ExistingSkeletalMeshPoseTrack.IsValid())
				{
					TSharedPtr<FSkeletalMeshPoseTrack> SkeletalMeshPoseTrack = MakeShared<FSkeletalMeshPoseTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
					SkeletalMeshPoseTrack->SetVisibilityFlag(bAnimationTracksEnabled);
					SkeletalMeshPoseTracks.Add(SkeletalMeshPoseTrack.ToSharedRef());

					InTimingViewSession.AddScrollableTrack(SkeletalMeshPoseTrack);
					GameplaySharedData.InvalidateObjectTracksOrder();

					ObjectEventsTrack->GetGameplayTrack().AddChildTrack(SkeletalMeshPoseTrack->GetGameplayTrack());
				}
			});

			AnimationProvider->EnumerateTickRecordTimelines(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession, &GameplayProvider](uint64 InAssetId, int32 InNodeId, const IAnimationProvider::TickRecordTimeline& InTimeline)
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

				TSharedPtr<FAnimationTickRecordsTrack> ExistingAnimationTickRecordsTrack = StaticCastSharedPtr<FAnimationTickRecordsTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindTickRecordTrackWithAssetId));
				if(!ExistingAnimationTickRecordsTrack.IsValid())
				{
					const FObjectInfo* AssetObjectInfo = GameplayProvider->FindObjectInfo(InAssetId);
					FString AssetName = AssetObjectInfo ? AssetObjectInfo->Name : LOCTEXT("UnknownAsset", "Unknown").ToString();
					TSharedPtr<FAnimationTickRecordsTrack> AnimationTickRecordsTrack = MakeShared<FAnimationTickRecordsTrack>(*this, InObjectInfo.Id, InAssetId, InNodeId, *AssetName);
					AnimationTickRecordsTrack->SetVisibilityFlag(bAnimationTracksEnabled);
					AnimationTickRecordsTracks.Add(AnimationTickRecordsTrack.ToSharedRef());

					InTimingViewSession.AddScrollableTrack(AnimationTickRecordsTrack);
					GameplaySharedData.InvalidateObjectTracksOrder();

					ObjectEventsTrack->GetGameplayTrack().AddChildTrack(AnimationTickRecordsTrack->GetGameplayTrack());
				}
			});

			AnimationProvider->ReadAnimGraphTimeline(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession](const IAnimationProvider::AnimGraphTimeline& InTimeline)
			{
				ObjectEventsTrack->SetVisibilityFlag(GameplaySharedData.AreGameplayTracksEnabled());

				auto FindAnimNodesTrack = [](const FBaseTimingTrack& InTrack)
				{
					if (InTrack.GetType() == FAnimNodesTrack::TypeName &&
						InTrack.GetSubType() == FAnimNodesTrack::SubTypeName)
					{
						return true;
					}

					return false;
				};

				TSharedPtr<FAnimNodesTrack> ExistingAnimNodesTrack = StaticCastSharedPtr<FAnimNodesTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindAnimNodesTrack));
				if(!ExistingAnimNodesTrack.IsValid())
				{
					TSharedPtr<FAnimNodesTrack> AnimNodesTrack = MakeShared<FAnimNodesTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
					AnimNodesTrack->SetVisibilityFlag(bAnimationTracksEnabled);
					AnimNodesTracks.Add(AnimNodesTrack.ToSharedRef());

					InTimingViewSession.AddScrollableTrack(AnimNodesTrack);
					GameplaySharedData.InvalidateObjectTracksOrder();

					ObjectEventsTrack->GetGameplayTrack().AddChildTrack(AnimNodesTrack->GetGameplayTrack());
				}
			});
		});
	}

	// Prevent mouse movement throttling if we are drawing stuff that can change when the mouse is dragged
	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		if(PoseTrack->IsVisible())
		{
			if(PoseTrack->ShouldDrawPose())
			{
				InTimingViewSession.PreventThrottling();
				break;
			}
		}
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

void FAnimationSharedData::OnTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker)
{
	bTimeMarkerValid = InTimeMarker != std::numeric_limits<double>::infinity();
	MarkerTime = InTimeMarker;

#if WITH_EDITOR
	for(const TSharedRef<FAnimNodesTrack> AnimNodesTrack : AnimNodesTracks)
	{
		AnimNodesTrack->UpdateDebugData(InTimeMarker);
	}

	InvalidateViewports();

	// Update pose tracks even if they are disabled, as they may be being debugged
	UWorld* WorldToUse = FGameplayTimingViewExtender::GetWorldToVisualize();
	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		if(bTimeMarkerValid && PoseTrack->IsPotentiallyDebugged())
		{
			PoseTrack->DrawPoses(WorldToUse, MarkerTime);
		}
	}
#endif
}

#if WITH_EDITOR
void FAnimationSharedData::InvalidateViewports()
{
	UEditorEngine* Engine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && Engine != nullptr)
	{
		for (FEditorViewportClient* ViewportClient : Engine->GetAllViewportClients())
		{
			if (ViewportClient)
			{
				ViewportClient->Invalidate();
			}
		}
	}
}
#endif

void FAnimationSharedData::EnumerateSkeletalMeshPoseTracks(TFunctionRef<void(const TSharedRef<FSkeletalMeshPoseTrack>&)> InCallback) const
{
	for(const TSharedRef<FSkeletalMeshPoseTrack>& Track : SkeletalMeshPoseTracks)
	{
		InCallback(Track);
	}
}

TSharedPtr<FSkeletalMeshPoseTrack> FAnimationSharedData::FindSkeletalMeshPoseTrack(uint64 InComponentId) const
{
	TSharedPtr<FSkeletalMeshPoseTrack> FoundTrack;

	EnumerateSkeletalMeshPoseTracks([&FoundTrack, InComponentId](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
	{
		if(InTrack->GetGameplayTrack().GetObjectId() == InComponentId)
		{
			FoundTrack = InTrack;
		}
	});

	return FoundTrack;
}

void FAnimationSharedData::EnumerateAnimNodesTracks(TFunctionRef<void(const TSharedRef<FAnimNodesTrack>&)> InCallback) const
{
	for(const TSharedRef<FAnimNodesTrack>& Track : AnimNodesTracks)
	{
		InCallback(Track);
	}
}

TSharedPtr<FAnimNodesTrack> FAnimationSharedData::FindAnimNodesTrack(uint64 InAnimInstanceId) const
{
	TSharedPtr<FAnimNodesTrack> FoundTrack;

	EnumerateAnimNodesTracks([&FoundTrack, InAnimInstanceId](const TSharedRef<FAnimNodesTrack>& InTrack)
	{
		if(InTrack->GetGameplayTrack().GetObjectId() == InAnimInstanceId)
		{
			FoundTrack = InTrack;
		}
	});

	return FoundTrack;
}

#if WITH_ENGINE

void FAnimationSharedData::DrawPoses(UWorld* InWorld)
{
	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		if(PoseTrack->IsVisible())
		{
			if(bTimeMarkerValid && PoseTrack->ShouldDrawPose())
			{
				PoseTrack->DrawPoses(InWorld, MarkerTime);
			}
		}
	}
}

#endif

#if WITH_EDITOR

void FAnimationSharedData::GetCustomDebugObjects(const IAnimationBlueprintEditor& InAnimationBlueprintEditor, TArray<FCustomDebugObject>& OutDebugList)
{
	for(const TSharedRef<FAnimNodesTrack> AnimNodesTrack : AnimNodesTracks)
	{
		if(AnimNodesTrack->IsVisible())
		{
			AnimNodesTrack->GetCustomDebugObjects(InAnimationBlueprintEditor, OutDebugList);
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE