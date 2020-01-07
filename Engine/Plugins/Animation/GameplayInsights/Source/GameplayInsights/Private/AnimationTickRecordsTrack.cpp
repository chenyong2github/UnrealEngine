// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

const FName FAnimationTickRecordsTrack::TypeName(TEXT("Graph"));
const FName FAnimationTickRecordsTrack::SubTypeName(TEXT("Animation.TickRecords"));

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

void FTickRecordSeries::UpdateAutoZoom(const FTimingTrackViewport& InViewport, const FAnimationTickRecordsTrack& InTrack)
{
	const float TimelineDY = FMath::Max(1.0f, InViewport.GetLayout().TimelineDY);
	const float TopY = FMath::Max(1.0f, TimelineDY);
	const float BottomY = FMath::Max(TopY, InTrack.GetHeight() - TimelineDY);

	FGraphSeries::UpdateAutoZoom(TopY, BottomY, CurrentMin, CurrentMax);
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
	: TGameplayTrackMixin<FGraphTrack>(InObjectID, FAnimationTickRecordsTrack::SubTypeName, MakeTrackName(InSharedData.GetGameplaySharedData(), InAssetId, InName))
	, SharedData(InSharedData)
	, AssetId(InAssetId)
	, NodeId(InNodeId)
	, RequestedTrackSizeScale(1.0f)
{
	uint32 Hash = GetTypeHash(GetName());
	MainSeriesLineColor = MakeSeriesColor(Hash, true);
	MainSeriesFillColor = MakeSeriesColor(Hash);

	AddAllSeries();

	bDrawPoints = false;
	bDrawBoxes = false;
	bDrawBaseline = false;
	bUseEventDuration = false;

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
				InstanceClass = FindObject<UAnimBlueprintGeneratedClass>(ANY_PACKAGE, AnimInstanceClassInfo->PathName);
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
static void UpdateSeries(const FAnimationSharedData& InSharedData, FAnimationTickRecordsTrack& InTrack, FTickRecordSeries& InSeries, const FTimingTrackViewport& InViewport, ProjectionType Projection)
{		
	const FGameplayProvider* GameplayProvider = InSharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	const FAnimationProvider* AnimationProvider = InSharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(GameplayProvider && AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(InSharedData.GetAnalysisSession());

		// Calc visible range before we build events (as builder uses scale factors calculated in auto zoom!)
		InSeries.CurrentMin = 0.0;
		InSeries.CurrentMax = 0.0;

		AnimationProvider->ReadTickRecordTimeline(InTrack.GetGameplayTrack().GetObjectId(), InTrack.GetAssetId(), InTrack.GetNodeId(), [&InViewport, &InSeries, &Projection](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InViewport.GetStartTime(), InViewport.GetEndTime(), [&InSeries, &Projection](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				const double Value = Invoke(Projection, InMessage);
				InSeries.CurrentMin = FMath::Min(InSeries.CurrentMin, Value);
				InSeries.CurrentMax = FMath::Max(InSeries.CurrentMax, Value);
			});
		});

		InSeries.UpdateAutoZoom(InViewport, InTrack);

		FGraphTrackBuilder Builder(InTrack, InSeries, InViewport);

		AnimationProvider->ReadTickRecordTimeline(InTrack.GetGameplayTrack().GetObjectId(), InTrack.GetAssetId(), InTrack.GetNodeId(), [&InTrack, &GameplayProvider, &AnimationProvider, &Builder, &InViewport, &Projection](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InViewport.GetStartTime(), InViewport.GetEndTime(), [&InTrack, &Builder, &GameplayProvider, &Projection](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				Builder.AddEvent(InStartTime, InEndTime - InStartTime, Invoke(Projection, InMessage), InMessage.bContinuous);

				InTrack.HeightInLanes = 1;
			});
		});
	}
}

void FAnimationTickRecordsTrack::UpdateTrackHeight(const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	const float CurrentTrackHeight = GetHeight();
	const float DesiredTrackHeight = Viewport.GetLayout().ComputeTrackHeight(HeightInLanes) * RequestedTrackSizeScale;

	if (CurrentTrackHeight < DesiredTrackHeight)
	{
		float NewTrackHeight;
		if (Viewport.IsDirty(ETimingTrackViewportDirtyFlags::VLayoutChanged))
		{
			NewTrackHeight = DesiredTrackHeight;
		}
		else
		{
			NewTrackHeight = FMath::CeilToFloat(CurrentTrackHeight * 0.9f + DesiredTrackHeight * 0.1f);
		}

		SetHeight(NewTrackHeight);

		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			StaticCastSharedPtr<FTickRecordSeries>(Series)->UpdateAutoZoom(Context.GetViewport(), *this);
		}
	}
	else if (CurrentTrackHeight > DesiredTrackHeight)
	{
		float NewTrackHeight;
		if (Viewport.IsDirty(ETimingTrackViewportDirtyFlags::VLayoutChanged))
		{
			NewTrackHeight = DesiredTrackHeight;
		}
		else
		{
			NewTrackHeight = FMath::FloorToFloat(CurrentTrackHeight * 0.9f + DesiredTrackHeight * 0.1f);
		}

		SetHeight(NewTrackHeight);

		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			StaticCastSharedPtr<FTickRecordSeries>(Series)->UpdateAutoZoom(Context.GetViewport(), *this);
		}
	}
}

void FAnimationTickRecordsTrack::PreUpdate(const ITimingTrackUpdateContext& Context)
{
	FGraphTrack::PreUpdate(Context);

	// update border size
	BorderY = Context.GetViewport().GetLayout().TimelineDY;

	const bool bIsEntireGraphTrackDirty = IsDirty() || Context.GetViewport().IsHorizontalViewportDirty();
	bool bNeedsUpdate = bIsEntireGraphTrackDirty;

	if (!bNeedsUpdate)
	{
		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			if (Series->IsVisible() && Series->IsDirty())
			{
				// At least one series is dirty.
				bNeedsUpdate = true;
				break;
			}
		}
	}

	if (bNeedsUpdate)
	{
		ClearDirtyFlag();

		HeightInLanes = 0;

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			if (Series->IsVisible() && (bIsEntireGraphTrackDirty || Series->IsDirty()))
			{
				// Clear the flag before updating, because the update itself may further need to set the series as dirty.
				Series->ClearDirtyFlag();

				TSharedPtr<FTickRecordSeries> TickRecordSeries = StaticCastSharedPtr<FTickRecordSeries>(Series);
				switch (TickRecordSeries->Type)
				{
				case FTickRecordSeries::ESeriesType::BlendWeight:
					UpdateSeries(SharedData, *this, *TickRecordSeries, Viewport, &FTickRecordMessage::BlendWeight);
					break;
				case FTickRecordSeries::ESeriesType::PlaybackTime:
					UpdateSeries(SharedData, *this, *TickRecordSeries, Viewport, &FTickRecordMessage::PlaybackTime);
					break;
				case FTickRecordSeries::ESeriesType::RootMotionWeight:
					UpdateSeries(SharedData, *this, *TickRecordSeries, Viewport, &FTickRecordMessage::RootMotionWeight);
					break;
				case FTickRecordSeries::ESeriesType::PlayRate:
					UpdateSeries(SharedData, *this, *TickRecordSeries, Viewport, &FTickRecordMessage::PlayRate);
					break;
				case FTickRecordSeries::ESeriesType::BlendSpacePositionX:
					UpdateSeries(SharedData, *this, *TickRecordSeries, Viewport, &FTickRecordMessage::BlendSpacePositionX);
					break;
				case FTickRecordSeries::ESeriesType::BlendSpacePositionY:
					UpdateSeries(SharedData, *this, *TickRecordSeries, Viewport, &FTickRecordMessage::BlendSpacePositionY);
					break;
				}
			}
		}

		UpdateStats();
	}

	UpdateTrackHeight(Context);
}

void FAnimationTickRecordsTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	FGraphTrack::Draw(Context);
	GetGameplayTrack().DrawHeaderForTimingTrack(Context, *this, true);
}

void FAnimationTickRecordsTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const
{
	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.GetStartTime(), HoveredTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch);

	FindTickRecordMessage(SearchParameters, [this, &Tooltip](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(GetName());

		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(InFoundStartTime).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("BlendWeight", "Blend Weight").ToString(), FText::AsNumber(InMessage.BlendWeight).ToString());
		if(InMessage.bIsBlendSpace)
		{
			Tooltip.AddNameValueTextLine(LOCTEXT("BlendSpacePositionX", "X").ToString(), FText::AsNumber(InMessage.BlendSpacePositionX).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("BlendSpacePositionX", "Y").ToString(), FText::AsNumber(InMessage.BlendSpacePositionY).ToString());
		}
		Tooltip.AddNameValueTextLine(LOCTEXT("PlaybackTime", "Playback Time").ToString(), FText::AsNumber(InMessage.PlaybackTime).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("RootMotionWeight", "Root Motion Weight").ToString(), FText::AsNumber(InMessage.RootMotionWeight).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("PlayRate", "Play Rate").ToString(), FText::AsNumber(InMessage.PlayRate).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("Looping", "Looping").ToString(), InMessage.bLooping ? LOCTEXT("True", "True").ToString() : LOCTEXT("False", "False").ToString());

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
					if(InstanceClass)
					{
						if(UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(InstanceClass->ClassGeneratedBy))
						{
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(AnimBlueprint);

							if(IAnimationBlueprintEditor* AnimBlueprintEditor = static_cast<IAnimationBlueprintEditor*>(GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(AnimBlueprint, true)))
							{
								int32 AnimNodeIndex = InstanceClass->AnimNodeProperties.Num() - NodeId - 1;
								TWeakObjectPtr<const UEdGraphNode> GraphNode = InstanceClass->AnimBlueprintDebugData.NodePropertyIndexToNodeMap[AnimNodeIndex];
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

	FGraphTrack::BuildContextMenu(MenuBuilder);

	MenuBuilder.BeginSection("TrackSize", LOCTEXT("TrackSizeMenuHeader", "Track Size"));
	{
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("NormalTrack", "Normal"),
			LOCTEXT("NormalTrack_Tooltip", "Draw this track at the standard size."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ RequestedTrackSizeScale = 1.0f; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return RequestedTrackSizeScale == 1.0f; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("LargeTrack", "Large"),
			LOCTEXT("LargeTrack_Tooltip", "Make this track larger than normal."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ RequestedTrackSizeScale = 2.0f; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return RequestedTrackSizeScale == 2.0f; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ExtraLargeTrack", "Extra Large"),
			LOCTEXT("ExtraLargeTrack_Tooltip", "Make this track much larger than normal."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ RequestedTrackSizeScale = 4.0f; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return RequestedTrackSizeScale == 4.0f; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();
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