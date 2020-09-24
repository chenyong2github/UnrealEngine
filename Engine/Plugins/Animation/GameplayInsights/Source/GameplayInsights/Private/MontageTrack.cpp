// Copyright Epic Games, Inc. All Rights Reserved.

#include "MontageTrack.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "AnimationSharedData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Modules/ModuleManager.h"
#include "Insights/ViewModels/GraphTrackBuilder.h"
#include "VariantTreeNode.h"
#include "TraceServices/Model/Frames.h"

#define LOCTEXT_NAMESPACE "MontageTrack"

INSIGHTS_IMPLEMENT_RTTI(FMontageTrack)

FMontageTrack::FMontageTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, const TCHAR* InName)
	: FGameplayGraphTrack(InSharedData.GetGameplaySharedData(), InObjectID, FText::Format(LOCTEXT("TrackNameFormat", "Montage - {0}"), FText::FromString(FString(InName))))
	, SharedData(InSharedData)
{
	EnableOptions(ShowLabelsOption);
	Layout = EGameplayGraphLayout::Stack;
}

void FMontageTrack::AddAllSeries()
{
	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(AnimationProvider && GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->EnumerateMontageIds(GetGameplayTrack().GetObjectId(), [this, &AnimationProvider, &GameplayProvider](uint64 InMontageId)
		{
			if(!AllSeries.ContainsByPredicate([InMontageId](const TSharedPtr<FGraphSeries>& InSeries){ return StaticCastSharedPtr<FMontageSeries>(InSeries)->MontageId == InMontageId; }))
			{
				auto MakeCurveSeriesColor = [](uint64 InSeed, bool bInLine)
				{
					FRandomStream Stream(GetTypeHash(InSeed));
					const uint8 Hue = (uint8)(Stream.FRand() * 255.0f);
					const uint8 SatVal = bInLine ? 196 : 128;
					return FLinearColor::MakeFromHSV8(Hue, SatVal, SatVal);
				};

				TSharedRef<FMontageSeries> Series = MakeShared<FMontageSeries>();

				const FObjectInfo& MontageInfo = GameplayProvider->GetObjectInfo(InMontageId);

				Series->SetName(MontageInfo.Name);
				Series->SetDescription(FText::Format(LOCTEXT("MontageTooltipFormat", "Weight for montage '{0}'"), FText::FromString(MontageInfo.Name)));

				const FLinearColor LineColor = MakeCurveSeriesColor(InMontageId, true);
				const FLinearColor FillColor = MakeCurveSeriesColor(InMontageId, false);
				Series->SetColor(LineColor, LineColor, FillColor);

				Series->MontageId = InMontageId;
				Series->SetVisibility(true);
				Series->SetBaselineY(25.0f);
				Series->SetScaleY(20.0f);
				Series->EnableAutoZoom();
				AllSeries.Add(Series);
			}
		});
	}
}

bool FMontageTrack::UpdateSeriesBounds(FGameplayGraphSeries& InSeries, const FTimingTrackViewport& InViewport)
{
	bool bFoundEvents = false;

	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		FMontageSeries& MontageSeries = *static_cast<FMontageSeries*>(&InSeries);

		MontageSeries.CurrentMin = 0.0f;
		MontageSeries.CurrentMax = 0.0f;

		AnimationProvider->ReadMontageTimeline(GetGameplayTrack().GetObjectId(), [&bFoundEvents, &InViewport, &MontageSeries, &AnimationProvider](const FAnimationProvider::AnimMontageTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InViewport.GetStartTime(), InViewport.GetEndTime(), [&bFoundEvents, &MontageSeries, &AnimationProvider](double InStartTime, double InEndTime, uint32 InDepth, const FAnimMontageMessage& InMessage)
			{
				if(InMessage.MontageId == MontageSeries.MontageId)
				{
					MontageSeries.CurrentMin = FMath::Min(MontageSeries.CurrentMin, InMessage.Weight);
					MontageSeries.CurrentMax = FMath::Max(MontageSeries.CurrentMax, InMessage.Weight);
					bFoundEvents = true;
				}
				return Trace::EEventEnumerate::Continue;
			});
		});
	}

	return bFoundEvents;
}

void FMontageTrack::UpdateSeries(FGameplayGraphSeries& InSeries, const FTimingTrackViewport& InViewport)
{
	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		FMontageSeries& MontageSeries = *static_cast<FMontageSeries*>(&InSeries);

		FGraphTrackBuilder Builder(*this, MontageSeries, InViewport);

		AnimationProvider->ReadMontageTimeline(GetGameplayTrack().GetObjectId(), [this, &Builder, &InViewport, &MontageSeries, &AnimationProvider](const FAnimationProvider::AnimMontageTimeline& InTimeline)
		{
			uint16 FrameCounter = 0;
			uint16 LastFrameWithMontage = 0;

			InTimeline.EnumerateEvents(InViewport.GetStartTime(), InViewport.GetEndTime(), [this, &FrameCounter, &LastFrameWithMontage, &Builder, &MontageSeries, &AnimationProvider](double InStartTime, double InEndTime, uint32 InDepth, const FAnimMontageMessage& InMessage)
			{
				FrameCounter = InMessage.FrameCounter;

				if(InMessage.MontageId == MontageSeries.MontageId)
				{
					Builder.AddEvent(InStartTime, InEndTime - InStartTime, InMessage.Weight, LastFrameWithMontage == FrameCounter - 1);

					LastFrameWithMontage = FrameCounter;
				}
				return Trace::EEventEnumerate::Continue;
			});
		});
	}
}

void FMontageTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const
{
	const FGraphTrackEvent& GraphTrackEvent = *static_cast<const FGraphTrackEvent*>(&HoveredTimingEvent);

	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.GetStartTime(), HoveredTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch);

	FindMontageMessage(SearchParameters, [this, &Tooltip, &GraphTrackEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FAnimMontageMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(GetName());

		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(InFoundStartTime).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("EventWeight", "Weight").ToString(), FText::AsNumber(GraphTrackEvent.GetValue()).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("EventDesiredWeight", "Desired Weight").ToString(), FText::AsNumber(InMessage.DesiredWeight).ToString());

		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
			if(GameplayProvider)
			{
				const FObjectInfo& MontageInfo = GameplayProvider->GetObjectInfo(InMessage.MontageId);
				Tooltip.AddNameValueTextLine(LOCTEXT("MontageName", "Montage").ToString(), MontageInfo.PathName);
			}

			const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
			if(AnimationProvider)
			{
				const TCHAR* CurrentSectionName = AnimationProvider->GetName(InMessage.CurrentSectionNameId);
				Tooltip.AddNameValueTextLine(LOCTEXT("CurrentSectionName", "Current Section").ToString(), FText::FromString(CurrentSectionName).ToString());
				const TCHAR* NextSectionName = AnimationProvider->GetName(InMessage.NextSectionNameId);
				Tooltip.AddNameValueTextLine(LOCTEXT("NextSectionName", "Next Section").ToString(), FText::FromString(NextSectionName).ToString());
			}
		}

		Tooltip.AddNameValueTextLine(LOCTEXT("EventWorld", "World").ToString(), GetGameplayTrack().GetWorldName(SharedData.GetAnalysisSession()).ToString());

		Tooltip.UpdateLayout();
	});
}

const TSharedPtr<const ITimingEvent> FMontageTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindMontageMessage(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FAnimMontageMessage& InFoundMessage)
	{
		FoundEvent = MakeShared<const FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

void FMontageTrack::FindMontageMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FAnimMontageMessage&)> InFoundPredicate) const
{
	TTimingEventSearch<FAnimMontageMessage>::Search(
		InParameters,

		[this](TTimingEventSearch<FAnimMontageMessage>::FContext& InContext)
		{
			const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

			if(AnimationProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

				AnimationProvider->ReadMontageTimeline(GetGameplayTrack().GetObjectId(), [&InContext](const FAnimationProvider::AnimMontageTimeline& InTimeline)
				{
					InTimeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FAnimMontageMessage& InMessage)
					{
						InContext.Check(InEventStartTime, InEventEndTime, 0, InMessage);
						return Trace::EEventEnumerate::Continue;
					});
				});
			}
		},

		[&InParameters](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FAnimMontageMessage& InEvent)
		{
			// Match the start time exactly here
			return InFoundStartTime == InParameters.StartTime;
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FAnimMontageMessage& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		},
		
		TTimingEventSearch<FAnimMontageMessage>::NoMatch);
}

void FMontageTrack::GetVariantsAtFrame(const Trace::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const 
{
	TSharedRef<FVariantTreeNode> Header = OutVariants.Add_GetRef(FVariantTreeNode::MakeHeader(LOCTEXT("MontagesHeader", "Montages"), 0));

	const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(SharedData.GetAnalysisSession());
	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(AnimationProvider && GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadMontageTimeline(GetGameplayTrack().GetObjectId(), [this, &InFrame, &AnimationProvider, &GameplayProvider, &Header](const FAnimationProvider::AnimMontageTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InFrame.StartTime, InFrame.EndTime, [this, &AnimationProvider, &GameplayProvider, &Header, &InFrame](double InStartTime, double InEndTime, uint32 InDepth, const FAnimMontageMessage& InMessage)
			{
				if(InStartTime >= InFrame.StartTime && InEndTime <= InFrame.EndTime)
				{
					const FObjectInfo& MontageInfo = GameplayProvider->GetObjectInfo(InMessage.MontageId);
					TSharedRef<FVariantTreeNode> MontageHeader = Header->AddChild(FVariantTreeNode::MakeObject(FText::FromString(MontageInfo.Name), InMessage.MontageId));

					MontageHeader->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("EventWeight", "Weight"), InMessage.Weight));
					MontageHeader->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("EventDesiredWeight", "Desired Weight"), InMessage.DesiredWeight));

					const TCHAR* CurrentSectionName = AnimationProvider->GetName(InMessage.CurrentSectionNameId);
					MontageHeader->AddChild(FVariantTreeNode::MakeString(LOCTEXT("CurrentSectionName", "Current Section"), CurrentSectionName));
					const TCHAR* NextSectionName = AnimationProvider->GetName(InMessage.NextSectionNameId);
					MontageHeader->AddChild(FVariantTreeNode::MakeString(LOCTEXT("NextSectionName", "Next Section"), NextSectionName));
				}
				return Trace::EEventEnumerate::Continue;
			});
		});
	}
}

#undef LOCTEXT_NAMESPACE
