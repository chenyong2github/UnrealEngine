// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationTickRecordsTrack.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "AnimationSharedData.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Templates/Invoke.h"
#include "Insights/ViewModels/GraphTrackBuilder.h"
#include "Insights/Common/TimeUtils.h"
#include "GameplaySharedData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "IAnimationBlueprintEditor.h"
#include "Animation/AnimBlueprint.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EdGraph/EdGraphNode.h"
#endif

#define LOCTEXT_NAMESPACE "AnimationTickRecordsTrack"

INSIGHTS_IMPLEMENT_RTTI(FAnimationTickRecordsTrack)

FString FTickRecordSeries::FormatValue(double Value) const
{
	switch (Type)
	{
	case ESeriesType::PlaybackTime:
		return TimeUtils::FormatTimeAuto(Value);
	case ESeriesType::BlendWeight:
	case ESeriesType::RootMotionWeight:
	case ESeriesType::PlayRate:
	case ESeriesType::BlendSpacePositionX:
	case ESeriesType::BlendSpacePositionY:
		return FText::AsNumber(Value).ToString();
	}

	return FGraphSeries::FormatValue(Value);
}

static FLinearColor MakeSeriesColor(uint32 InSeed, bool bInLine = false)
{
	FRandomStream Stream(InSeed);
	const uint8 Hue = (uint8)(Stream.FRand() * 255.0f);
	const uint8 SatVal = bInLine ? 196 : 128;
	return FLinearColor::MakeFromHSV8(Hue, SatVal, SatVal);
}

static FLinearColor MakeSeriesColor(FTickRecordSeries::ESeriesType InSeed, bool bInLine = false)
{
	return MakeSeriesColor((uint32)InSeed, bInLine);
}

FAnimationTickRecordsTrack::FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, uint64 InAssetId, int32 InNodeId, const TCHAR* InName)
	: FGameplayGraphTrack(InObjectID, MakeTrackName(InSharedData.GetGameplaySharedData(), InAssetId, InName))
	, SharedData(InSharedData)
	, AssetId(InAssetId)
	, NodeId(InNodeId)
{
	uint32 Hash = GetTypeHash(GetName());
	MainSeriesLineColor = MakeSeriesColor(Hash, true);
	MainSeriesFillColor = MakeSeriesColor(Hash);

#if WITH_EDITOR
	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(GameplayProvider)
	{
		const FObjectInfo* AnimInstanceInfo = GameplayProvider->FindObjectInfo(GetGameplayTrack().GetObjectId());
		if(AnimInstanceInfo)
		{
			const FClassInfo* AnimInstanceClassInfo = GameplayProvider->FindClassInfo(AnimInstanceInfo->ClassId);
			if(AnimInstanceClassInfo)
			{
				InstanceClass = FSoftObjectPath(AnimInstanceClassInfo->PathName);
			}
		}
	}
#endif
}

void FAnimationTickRecordsTrack::AddAllSeries()
{
	struct FSeriesDescription
	{
		FText Name;
		FText Description;
		FLinearColor LineColor;
		FLinearColor FillColor;
		FTickRecordSeries::ESeriesType Type;
		bool bEnabled;
	};

	const FSeriesDescription SeriesDescriptions[] =
	{
		{ 
			LOCTEXT("SeriesNameBlendWeight", "Blend Weight"), 
			LOCTEXT("SeriesDescBlendWeight", "The final effective weight that this animation sequence was played at"), 
			MainSeriesLineColor, 
			MainSeriesFillColor, 
			FTickRecordSeries::ESeriesType::BlendWeight,
			true
		},
		{ 
			LOCTEXT("SeriesNamePlaybackTime", "Playback Time"), 
			LOCTEXT("SeriesDescPlaybackTime", "The playback time of this animation sequence"), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::PlaybackTime, true), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::PlaybackTime),
			FTickRecordSeries::ESeriesType::PlaybackTime,
			false
		},
		{ 
			LOCTEXT("SeriesNameRootMotionWeight", "Root Motion Weight"),
			LOCTEXT("SeriesDescRootMotionWeight", "The final effective root motion weight that this animation sequence was played at"), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::RootMotionWeight, true), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::RootMotionWeight), 
			FTickRecordSeries::ESeriesType::RootMotionWeight, 
			false
		},
		{ 
			LOCTEXT("SeriesNamePlayRate", "Play Rate"), 
			LOCTEXT("SeriesDescPlayRate", "The play rate/speed of this animation sequence"), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::PlayRate, true),
			MakeSeriesColor(FTickRecordSeries::ESeriesType::PlayRate), 
			FTickRecordSeries::ESeriesType::PlayRate,
			false
		},
	};

	const FSeriesDescription BlendSpaceSeriesDescriptions[] =
	{
		{ 
			LOCTEXT("SeriesNameBlendSpacePositionX", "BlendSpace Position X"), 
			LOCTEXT("SeriesDescBlendSpacePositionX", "The X value used to sample this blend space"), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::BlendSpacePositionX, true),
			MakeSeriesColor(FTickRecordSeries::ESeriesType::BlendSpacePositionX),
			FTickRecordSeries::ESeriesType::BlendSpacePositionX, 
			false
		},
		{
			LOCTEXT("SeriesNameBlendSpacePositionY", "BlendSpace Position Y"), 
			LOCTEXT("SeriesDescBlendSpacePositionY", "The Y value used to sample this blend space"), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::BlendSpacePositionY, true), 
			MakeSeriesColor(FTickRecordSeries::ESeriesType::BlendSpacePositionY), 
			FTickRecordSeries::ESeriesType::BlendSpacePositionY, 
			false
		},
	};

	auto AddSeries = [this](const FSeriesDescription& InSeriesDescription)
	{
		TSharedRef<FTickRecordSeries> Series = MakeShared<FTickRecordSeries>();
		Series->SetName(InSeriesDescription.Name.ToString());
		Series->SetDescription(InSeriesDescription.Description.ToString());
		Series->SetColor(InSeriesDescription.LineColor, InSeriesDescription.LineColor, InSeriesDescription.FillColor);
		Series->Type = InSeriesDescription.Type;
		Series->SetVisibility(InSeriesDescription.bEnabled);
		Series->SetBaselineY(25.0f);
		Series->SetScaleY(20.0f);
		Series->EnableAutoZoom();
		AllSeries.Add(Series);	
	};

	for(const FSeriesDescription& SeriesDescription : SeriesDescriptions)
	{
		AddSeries(SeriesDescription);
	}

	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	if(GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		const FClassInfo& ClassInfo = GameplayProvider->GetClassInfoFromObject(AssetId);
		if(FCString::Stristr(ClassInfo.Name, TEXT("BlendSpace")) != nullptr)
		{
			for(const FSeriesDescription& BlendSpaceSeriesDescription : BlendSpaceSeriesDescriptions)
			{
				AddSeries(BlendSpaceSeriesDescription);
			}
		}
	}
}

template<typename ProjectionType>
bool FAnimationTickRecordsTrack::UpdateSeriesBoundsHelper(FTickRecordSeries& InSeries, const FTimingTrackViewport& InViewport, ProjectionType Projection)
{
	bool bFoundEvents = false;

	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		InSeries.CurrentMin = 0.0;
		InSeries.CurrentMax = 0.0;

		AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), GetAssetId(), GetNodeId(), [&bFoundEvents, &InViewport, &InSeries, &Projection](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InViewport.GetStartTime(), InViewport.GetEndTime(), [&bFoundEvents, &InSeries, &Projection](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				const float Value = Invoke(Projection, InMessage);
				InSeries.CurrentMin = FMath::Min(InSeries.CurrentMin, Value);
				InSeries.CurrentMax = FMath::Max(InSeries.CurrentMax, Value);
				bFoundEvents = true;
			});
		});
	}

	return bFoundEvents;
}

template<typename ProjectionType>
void FAnimationTickRecordsTrack::UpdateSeriesHelper(FTickRecordSeries& InSeries, const FTimingTrackViewport& InViewport, ProjectionType Projection)
{		
	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		FGraphTrackBuilder Builder(*this, InSeries, InViewport);

		AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), GetAssetId(), GetNodeId(), [this, &AnimationProvider, &Builder, &InViewport, &Projection](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InViewport.GetStartTime(), InViewport.GetEndTime(), [this, &Builder, &Projection](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				Builder.AddEvent(InStartTime, InEndTime - InStartTime, Invoke(Projection, InMessage), InMessage.bContinuous);
			});
		});
	}
}

bool FAnimationTickRecordsTrack::UpdateSeriesBounds(FGameplayGraphSeries& InSeries, const FTimingTrackViewport& InViewport)
{
	FTickRecordSeries& TickRecordSeries = *static_cast<FTickRecordSeries*>(&InSeries);
	switch (TickRecordSeries.Type)
	{
	case FTickRecordSeries::ESeriesType::BlendWeight:
		return UpdateSeriesBoundsHelper(TickRecordSeries, InViewport, &FTickRecordMessage::BlendWeight);
	case FTickRecordSeries::ESeriesType::PlaybackTime:
		return UpdateSeriesBoundsHelper(TickRecordSeries, InViewport, &FTickRecordMessage::PlaybackTime);
	case FTickRecordSeries::ESeriesType::RootMotionWeight:
		return UpdateSeriesBoundsHelper(TickRecordSeries, InViewport, &FTickRecordMessage::RootMotionWeight);
	case FTickRecordSeries::ESeriesType::PlayRate:
		return UpdateSeriesBoundsHelper(TickRecordSeries, InViewport, &FTickRecordMessage::PlayRate);
	case FTickRecordSeries::ESeriesType::BlendSpacePositionX:
		return UpdateSeriesBoundsHelper(TickRecordSeries, InViewport, &FTickRecordMessage::BlendSpacePositionX);
	case FTickRecordSeries::ESeriesType::BlendSpacePositionY:
		return UpdateSeriesBoundsHelper(TickRecordSeries, InViewport, &FTickRecordMessage::BlendSpacePositionY);
	}

	return false;
}

void FAnimationTickRecordsTrack::UpdateSeries(FGameplayGraphSeries& InSeries, const FTimingTrackViewport& InViewport)
{
	FTickRecordSeries& TickRecordSeries = *static_cast<FTickRecordSeries*>(&InSeries);
	switch (TickRecordSeries.Type)
	{
	case FTickRecordSeries::ESeriesType::BlendWeight:
		UpdateSeriesHelper(TickRecordSeries, InViewport, &FTickRecordMessage::BlendWeight);
		break;
	case FTickRecordSeries::ESeriesType::PlaybackTime:
		UpdateSeriesHelper(TickRecordSeries, InViewport, &FTickRecordMessage::PlaybackTime);
		break;
	case FTickRecordSeries::ESeriesType::RootMotionWeight:
		UpdateSeriesHelper(TickRecordSeries, InViewport, &FTickRecordMessage::RootMotionWeight);
		break;
	case FTickRecordSeries::ESeriesType::PlayRate:
		UpdateSeriesHelper(TickRecordSeries, InViewport, &FTickRecordMessage::PlayRate);
		break;
	case FTickRecordSeries::ESeriesType::BlendSpacePositionX:
		UpdateSeriesHelper(TickRecordSeries, InViewport, &FTickRecordMessage::BlendSpacePositionX);
		break;
	case FTickRecordSeries::ESeriesType::BlendSpacePositionY:
		UpdateSeriesHelper(TickRecordSeries, InViewport, &FTickRecordMessage::BlendSpacePositionY);
		break;
	}
}

void FAnimationTickRecordsTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const
{
	const FGraphTrackEvent& GraphTrackEvent = *static_cast<const FGraphTrackEvent*>(&HoveredTimingEvent);

	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.GetStartTime(), HoveredTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch);

	FindTickRecordMessage(SearchParameters, [this, &GraphTrackEvent, &Tooltip](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(GetName());

		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(InFoundStartTime).ToString());
		Tooltip.AddNameValueTextLine(GraphTrackEvent.GetSeries()->GetName().ToString(), FText::AsNumber(GraphTrackEvent.GetValue()).ToString());

		Tooltip.UpdateLayout();
	});
}

const TSharedPtr<const ITimingEvent> FAnimationTickRecordsTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindTickRecordMessage(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InFoundMessage)
	{
		FoundEvent = MakeShared<const FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

void FAnimationTickRecordsTrack::FindTickRecordMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FTickRecordMessage&)> InFoundPredicate) const
{
	TTimingEventSearch<FTickRecordMessage>::Search(
		InParameters,

		[this](TTimingEventSearch<FTickRecordMessage>::FContext& InContext)
		{
			const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

			if(AnimationProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

				AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), AssetId, NodeId, [this, &InContext](const FAnimationProvider::TickRecordTimeline& InTimeline)
				{
					InTimeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
					{
						InContext.Check(InEventStartTime, InEventEndTime, 0, InMessage);
					});
				});
			}
		},

		[&InParameters](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InEvent)
		{
			// Match the start time exactly here
			return InFoundStartTime == InParameters.StartTime;
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		},
			
		TTimingEventSearch<FTickRecordMessage>::NoMatch);
}

void FAnimationTickRecordsTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
#if WITH_EDITOR
	MenuBuilder.BeginSection("TrackActions", LOCTEXT("TrackActionsMenuHeader", "Track Actions"));
	{
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("FindAssetPlayerNode", "Find Asset Player Node"),
			LOCTEXT("FindAssetPlayerNode_Tooltip", "Open the animation blueprint that this animation was played from."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					if(InstanceClass.LoadSynchronous())
					{
						if(UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(InstanceClass.Get()->ClassGeneratedBy))
						{
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(AnimBlueprint);

							if(IAnimationBlueprintEditor* AnimBlueprintEditor = static_cast<IAnimationBlueprintEditor*>(GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(AnimBlueprint, true)))
							{
								int32 AnimNodeIndex = InstanceClass.Get()->AnimNodeProperties.Num() - NodeId - 1;
								TWeakObjectPtr<const UEdGraphNode> GraphNode = InstanceClass.Get()->AnimBlueprintDebugData.NodePropertyIndexToNodeMap[AnimNodeIndex];
								if(GraphNode.Get())
								{
									AnimBlueprintEditor->JumpToHyperlink(GraphNode.Get());
								}
							}
						}
					}
				})
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
#endif

	FGameplayGraphTrack::BuildContextMenu(MenuBuilder);
}

FText FAnimationTickRecordsTrack::MakeTrackName(const FGameplaySharedData& InSharedData, uint64 InAssetID, const TCHAR* InName) const
{
	FText AssetTypeName = LOCTEXT("UnknownAsset", "Unknown");

	const FGameplayProvider* GameplayProvider = InSharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	if(GameplayProvider)
	{
		const FClassInfo& ClassInfo = GameplayProvider->GetClassInfoFromObject(InAssetID);
		AssetTypeName = FText::FromString(ClassInfo.Name);
	}

	return FText::Format(LOCTEXT("AnimationTickRecordsTrackName", "{0} - {1}"), AssetTypeName, FText::FromString(InName));
}

#undef LOCTEXT_NAMESPACE