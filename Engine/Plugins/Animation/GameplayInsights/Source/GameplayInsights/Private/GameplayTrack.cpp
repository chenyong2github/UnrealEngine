// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Algo/Sort.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/Common/PaintUtils.h"
#include "Insights/ViewModels/ITimingViewDrawHelper.h"

namespace GameplayTrackConstants
{
	constexpr float IndentSize = 12.0f;
}

void FGameplayTrack::AddChildTrack(FGameplayTrack& InChildTrack)
{
	check(InChildTrack.Parent == nullptr);
	InChildTrack.Parent = this;

	Children.Add(&InChildTrack);

	Algo::Sort(Children, [](FGameplayTrack* InTrack0, FGameplayTrack* InTrack1)
	{
		return InTrack0->GetTimingTrack()->GetName() < InTrack1->GetTimingTrack()->GetName();
	});	
}

TSharedPtr<FBaseTimingTrack> FGameplayTrack::FindChildTrack(uint64 InObjectId, TFunctionRef<bool(const FBaseTimingTrack& InTrack)> Callback) const
{
	for(const FGameplayTrack* ChildTrack : Children)
	{
		if( ChildTrack != nullptr &&
			ChildTrack->ObjectId == InObjectId && 
			Callback(ChildTrack->GetTimingTrack().Get()))
		{
			return ChildTrack->GetTimingTrack();
		}
	}

	return nullptr;
}

static FORCEINLINE bool IntervalsIntersect(float Min1, float Max1, float Min2, float Max2)
{
	return !(Max2 < Min1 || Max1 < Min2);
}

void FGameplayTrack::DrawHeaderForTimingTrack(const ITimingTrackDrawContext& InContext, const FBaseTimingTrack& InTrack, bool bUsePreallocatedLayers) const
{
	const float X = (float)Indent * GameplayTrackConstants::IndentSize;
	const float Y = InTrack.GetPosY();
	const float H = InTrack.GetHeight();
	const float TrackNameH = H > 7.0f ? 12.0f : H;
	const int32 HeaderBackgroundLayerId = bUsePreallocatedLayers ? InContext.GetHelper().GetHeaderBackgroundLayerId() : InContext.GetDrawContext().LayerId++;
	const int32 HeaderTextLayerId = bUsePreallocatedLayers ? InContext.GetHelper().GetHeaderTextLayerId() : InContext.GetDrawContext().LayerId++;

	if (H > 0.0f &&
		Y + H > InContext.GetViewport().GetTopOffset() &&
		Y < InContext.GetViewport().GetHeight() - InContext.GetViewport().GetBottomOffset())
	{
		// Draw a horizontal line between timelines.
		InContext.GetDrawContext().DrawBox(HeaderBackgroundLayerId, X, Y, InContext.GetViewport().GetWidth(), 1.0f, InContext.GetHelper().GetWhiteBrush(), InContext.GetHelper().GetEdgeColor());

		if(H > 7.0f)
		{
			const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const float NameWidth = FontMeasureService->Measure(InTrack.GetName(), InContext.GetHelper().GetEventFont()).X;
			InContext.GetDrawContext().DrawBox(HeaderBackgroundLayerId, X, Y + 1.0f, NameWidth + 4.0f, TrackNameH, InContext.GetHelper().GetWhiteBrush(), InContext.GetHelper().GetEdgeColor());
			InContext.GetDrawContext().DrawText(HeaderTextLayerId, X + 2.0f, Y, InTrack.GetName(), InContext.GetHelper().GetEventFont(), InContext.GetHelper().GetTrackNameTextColor(InTrack));
		}
		else
		{
			InContext.GetDrawContext().DrawBox(HeaderBackgroundLayerId, X, Y + 1.0f, H, H, InContext.GetHelper().GetWhiteBrush(), InContext.GetHelper().GetEdgeColor());
		}
	}

	// Draw lines connecting to parent
	if(Parent && Parent->GetTimingTrack()->IsVisible())
	{
		const float ParentX = (float)Parent->GetIndent() * GameplayTrackConstants::IndentSize;
		const float ParentY = FMath::Max(InContext.GetViewport().GetTopOffset(), Parent->GetTimingTrack()->GetPosY());

		if (IntervalsIntersect(ParentY, Y, InContext.GetViewport().GetTopOffset(), InContext.GetViewport().GetHeight() - InContext.GetViewport().GetBottomOffset()))
		{
			InContext.GetDrawContext().DrawBox(HeaderBackgroundLayerId, ParentX, Y + (TrackNameH * 0.5f), X - ParentX, 1.0f, InContext.GetHelper().GetWhiteBrush(), InContext.GetHelper().GetEdgeColor());
			InContext.GetDrawContext().DrawBox(HeaderBackgroundLayerId, ParentX, ParentY, 1.0f, (Y - ParentY) + (TrackNameH * 0.5f), InContext.GetHelper().GetWhiteBrush(), InContext.GetHelper().GetEdgeColor());
		}
	}
}
