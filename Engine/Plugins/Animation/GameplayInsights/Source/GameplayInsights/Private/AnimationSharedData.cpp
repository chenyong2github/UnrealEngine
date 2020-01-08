// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationSharedData.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplaySharedData.h"
#include "ObjectEventsTrack.h"
#include "SkeletalMeshPoseTrack.h"
#include "SkeletalMeshCurvesTrack.h"
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
	, bSkeletalMeshPoseTracksEnabled(true)
	, bSkeletalMeshCurveTracksEnabled(true)
	, bTickRecordTracksEnabled(true)
	, bAnimNodeTracksEnabled(true)
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
			GameplayProvider->ReadObjectEventsTimeline(InObjectInfo.Id, [this, &InTimingViewSession, &InAnalysisSession, &InObjectInfo, &AnimationProvider, &GameplayProvider](const IGameplayProvider::ObjectEventsTimeline& InTimeline)
			{
				if(InTimeline.GetEventCount() > 0)
				{
					TSharedRef<FObjectEventsTrack> ObjectEventsTrack = GameplaySharedData.GetObjectEventsTrackForId(InTimingViewSession, InAnalysisSession, InObjectInfo);

					AnimationProvider->ReadSkeletalMeshPoseTimeline(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession](const IAnimationProvider::SkeletalMeshPoseTimeline& InTimeline, bool bInHasCurves)
					{
						auto FindSkeletalMeshPoseTrack = [](const FBaseTimingTrack& InTrack)
						{
							return InTrack.Is<FSkeletalMeshPoseTrack>();
						};

						TSharedPtr<FSkeletalMeshPoseTrack> ExistingSkeletalMeshPoseTrack = StaticCastSharedPtr<FSkeletalMeshPoseTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindSkeletalMeshPoseTrack));
						if(!ExistingSkeletalMeshPoseTrack.IsValid())
						{
							TSharedPtr<FSkeletalMeshPoseTrack> SkeletalMeshPoseTrack = MakeShared<FSkeletalMeshPoseTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
							SkeletalMeshPoseTrack->SetVisibilityFlag(bSkeletalMeshPoseTracksEnabled);
							SkeletalMeshPoseTracks.Add(SkeletalMeshPoseTrack.ToSharedRef());

							InTimingViewSession.AddScrollableTrack(SkeletalMeshPoseTrack);
							GameplaySharedData.InvalidateObjectTracksOrder();

							ObjectEventsTrack->GetGameplayTrack().AddChildTrack(SkeletalMeshPoseTrack->GetGameplayTrack());
						}

						if(bInHasCurves)
						{
							auto FindSkeletalMeshCurvesTrack = [](const FBaseTimingTrack& InTrack)
							{
								return InTrack.Is<FSkeletalMeshCurvesTrack>();
							};

							TSharedPtr<FSkeletalMeshCurvesTrack> ExistingSkeletalMeshCurvesTrack = StaticCastSharedPtr<FSkeletalMeshCurvesTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindSkeletalMeshCurvesTrack));
							if(!ExistingSkeletalMeshCurvesTrack.IsValid())
							{
								TSharedPtr<FSkeletalMeshCurvesTrack> SkeletalMeshCurvesTrack = MakeShared<FSkeletalMeshCurvesTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
								SkeletalMeshCurvesTrack->SetVisibilityFlag(bSkeletalMeshCurveTracksEnabled);
								SkeletalMeshCurvesTracks.Add(SkeletalMeshCurvesTrack.ToSharedRef());

								InTimingViewSession.AddScrollableTrack(SkeletalMeshCurvesTrack);
								GameplaySharedData.InvalidateObjectTracksOrder();

								ObjectEventsTrack->GetGameplayTrack().AddChildTrack(SkeletalMeshCurvesTrack->GetGameplayTrack());
							}
						}
					});

					AnimationProvider->EnumerateTickRecordTimelines(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession, &GameplayProvider](uint64 InAssetId, int32 InNodeId, const IAnimationProvider::TickRecordTimeline& InTimeline)
					{
						auto FindTickRecordTrackWithAssetId = [&InAssetId](const FBaseTimingTrack& InTrack)
						{
							if (InTrack.Is<FAnimationTickRecordsTrack>())
							{
								const FAnimationTickRecordsTrack& AnimationTickRecordsTrack = InTrack.As<FAnimationTickRecordsTrack>();
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
							AnimationTickRecordsTrack->SetVisibilityFlag(bTickRecordTracksEnabled);
							AnimationTickRecordsTracks.Add(AnimationTickRecordsTrack.ToSharedRef());

							InTimingViewSession.AddScrollableTrack(AnimationTickRecordsTrack);
							GameplaySharedData.InvalidateObjectTracksOrder();

							ObjectEventsTrack->GetGameplayTrack().AddChildTrack(AnimationTickRecordsTrack->GetGameplayTrack());
						}
					});

					AnimationProvider->ReadAnimGraphTimeline(InObjectInfo.Id, [this, &InObjectInfo, &ObjectEventsTrack, &InTimingViewSession](const IAnimationProvider::AnimGraphTimeline& InTimeline)
					{
						auto FindAnimNodesTrack = [](const FBaseTimingTrack& InTrack)
						{
							return InTrack.Is<FAnimNodesTrack>();
						};

						TSharedPtr<FAnimNodesTrack> ExistingAnimNodesTrack = StaticCastSharedPtr<FAnimNodesTrack>(ObjectEventsTrack->GetGameplayTrack().FindChildTrack(InObjectInfo.Id, FindAnimNodesTrack));
						if(!ExistingAnimNodesTrack.IsValid())
						{
							TSharedPtr<FAnimNodesTrack> AnimNodesTrack = MakeShared<FAnimNodesTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
							AnimNodesTrack->SetVisibilityFlag(bAnimNodeTracksEnabled);
							AnimNodesTracks.Add(AnimNodesTrack.ToSharedRef());

							InTimingViewSession.AddScrollableTrack(AnimNodesTrack);
							GameplaySharedData.InvalidateObjectTracksOrder();

							ObjectEventsTrack->GetGameplayTrack().AddChildTrack(AnimNodesTrack->GetGameplayTrack());
						}
					});
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
		LOCTEXT("ToggleAnimationTracks_Tooltip", "Show/hide all animation tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FAnimationSharedData::ToggleAnimationTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FAnimationSharedData::AreAnimationTracksEnabled)),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleSkelMeshPoseTracks", "Pose Tracks"),
		LOCTEXT("ToggleSkelMeshPoseTracks_Tooltip", "Show/hide the skeletal mesh pose tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FAnimationSharedData::ToggleSkeletalMeshPoseTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this](){ return bSkeletalMeshPoseTracksEnabled; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleSkelMeshCurveTracks", "Curve Tracks"),
		LOCTEXT("ToggleSkelMeshCurveTracks_Tooltip", "Show/hide the skeletal mesh curve tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FAnimationSharedData::ToggleSkeletalMeshCurveTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this](){ return bSkeletalMeshCurveTracksEnabled; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleAnimTickRecordTracks", "Blend Weights Tracks"),
		LOCTEXT("ToggleAnimTickRecordTracks_Tooltip", "Show/hide the blend weights (tick records) tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FAnimationSharedData::ToggleTickRecordTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this](){ return bTickRecordTracksEnabled; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleAnimNodeTracks", "Graph Tracks"),
		LOCTEXT("ToggleAnimNodeTracks_Tooltip", "Show/hide the animation graph tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FAnimationSharedData::ToggleAnimNodeTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this](){ return bAnimNodeTracksEnabled; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);
}

void FAnimationSharedData::ToggleAnimationTracks()
{
	bool bAnimationTracksEnabled = !AreAnimationTracksEnabled();

	bSkeletalMeshPoseTracksEnabled = bAnimationTracksEnabled;
	bSkeletalMeshCurveTracksEnabled = bAnimationTracksEnabled;
	bTickRecordTracksEnabled = bAnimationTracksEnabled;
	bAnimNodeTracksEnabled = bAnimationTracksEnabled;

	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		PoseTrack->SetVisibilityFlag(bAnimationTracksEnabled);
	}

	for(TSharedRef<FSkeletalMeshCurvesTrack> CurvesTrack : SkeletalMeshCurvesTracks)
	{
		CurvesTrack->SetVisibilityFlag(bAnimationTracksEnabled);
	}

	for(TSharedRef<FAnimationTickRecordsTrack> TickRecordTrack : AnimationTickRecordsTracks)
	{
		TickRecordTrack->SetVisibilityFlag(bAnimationTracksEnabled);
	}

	for(TSharedRef<FAnimNodesTrack> AnimNodesTrack : AnimNodesTracks)
	{
		AnimNodesTrack->SetVisibilityFlag(bAnimationTracksEnabled);
	}
}

bool FAnimationSharedData::AreAnimationTracksEnabled() const
{
	return bSkeletalMeshPoseTracksEnabled && bSkeletalMeshCurveTracksEnabled && bTickRecordTracksEnabled && bAnimNodeTracksEnabled;
}

void FAnimationSharedData::ToggleSkeletalMeshPoseTracks()
{
	bSkeletalMeshPoseTracksEnabled = !bSkeletalMeshPoseTracksEnabled;

	for(TSharedRef<FSkeletalMeshPoseTrack> PoseTrack : SkeletalMeshPoseTracks)
	{
		PoseTrack->SetVisibilityFlag(bSkeletalMeshPoseTracksEnabled);
	}
}

void FAnimationSharedData::ToggleSkeletalMeshCurveTracks()
{
	bSkeletalMeshCurveTracksEnabled = !bSkeletalMeshCurveTracksEnabled;

	for(TSharedRef<FSkeletalMeshCurvesTrack> CurvesTrack : SkeletalMeshCurvesTracks)
	{
		CurvesTrack->SetVisibilityFlag(bSkeletalMeshCurveTracksEnabled);
	}
}

void FAnimationSharedData::ToggleTickRecordTracks()
{
	bTickRecordTracksEnabled = !bTickRecordTracksEnabled;

	for(TSharedRef<FAnimationTickRecordsTrack> TickRecordsTrack : AnimationTickRecordsTracks)
	{
		TickRecordsTrack->SetVisibilityFlag(bTickRecordTracksEnabled);
	}
}

void FAnimationSharedData::ToggleAnimNodeTracks()
{
	bAnimNodeTracksEnabled = !bAnimNodeTracksEnabled;

	for(TSharedRef<FAnimNodesTrack> AnimNodesTrack : AnimNodesTracks)
	{
		AnimNodesTrack->SetVisibilityFlag(bAnimNodeTracksEnabled);
	}
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