// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplaySharedData.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "ObjectEventsTrack.h"
#include "Algo/Sort.h"
#include "GameplayProvider.h"
#include "Insights/ITimingViewSession.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SGameplayTrackTree.h"

#define LOCTEXT_NAMESPACE "GameplaySharedData"

FGameplaySharedData::FGameplaySharedData()
	: AnalysisSession(nullptr)
	, bObjectTracksDirty(false)
	, bObjectTracksEnabled(false)
{
}

void FGameplaySharedData::OnBeginSession(Insights::ITimingViewSession& InTimingViewSession)
{
	ObjectTracks.Reset();
}

void FGameplaySharedData::OnEndSession(Insights::ITimingViewSession& InTimingViewSession)
{
	ObjectTracks.Reset();
}

TSharedRef<FObjectEventsTrack> FGameplaySharedData::GetObjectEventsTrackForId(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession, const FObjectInfo& InObjectInfo)
{
	const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	check(GameplayProvider);

	TSharedPtr<FObjectEventsTrack> LeafObjectEventsTrack = ObjectTracks.FindRef(InObjectInfo.Id);
	if(!LeafObjectEventsTrack.IsValid())
	{
		LeafObjectEventsTrack = MakeShared<FObjectEventsTrack>(*this, InObjectInfo.Id, InObjectInfo.Name);
		LeafObjectEventsTrack->SetVisibilityFlag(bObjectTracksEnabled);
		ObjectTracks.Add(InObjectInfo.Id, LeafObjectEventsTrack);

		InTimingViewSession.AddScrollableTrack(LeafObjectEventsTrack);
		InvalidateObjectTracksOrder();
	}

	// Fill the outer chain
	if(InObjectInfo.OuterId != 0)
	{
		TSharedPtr<FObjectEventsTrack> ObjectEventsTrack = LeafObjectEventsTrack;
		uint64 OuterId = InObjectInfo.OuterId;
		while(const FObjectInfo* OuterInfo = GameplayProvider->FindObjectInfo(OuterId))
		{
			TSharedPtr<FObjectEventsTrack> OuterEventsTrack = ObjectTracks.FindRef(OuterId);
			if(!OuterEventsTrack.IsValid())
			{
				OuterEventsTrack = MakeShared<FObjectEventsTrack>(*this, OuterId, OuterInfo->Name);
				OuterEventsTrack->SetVisibilityFlag(bObjectTracksEnabled);
				ObjectTracks.Add(OuterId, OuterEventsTrack);

				InTimingViewSession.AddScrollableTrack(OuterEventsTrack);
				InvalidateObjectTracksOrder();
			}

			// setup hierarchy
			if(ObjectEventsTrack->GetGameplayTrack().GetParentTrack() == nullptr)
			{
				OuterEventsTrack->GetGameplayTrack().AddChildTrack(ObjectEventsTrack->GetGameplayTrack());
			}

			ObjectEventsTrack = OuterEventsTrack;
			OuterId = OuterInfo->OuterId;
		}
	}

	return LeafObjectEventsTrack.ToSharedRef();
}

static void UpdateTrackOrderRecursive(TSharedRef<FBaseTimingTrack> InTrack, int32& InOutOrder)
{
	if(InTrack->Is<FObjectEventsTrack>())
	{
		TSharedRef<FObjectEventsTrack> ObjectEventsTrack = StaticCastSharedRef<FObjectEventsTrack>(InTrack);

		// recurse down object-track children, then non-object track leaf tracks to set 
		// overall ordering based on depth-first traversal of the hierarchy

		ObjectEventsTrack->SetOrder(InOutOrder++);

		for(FGameplayTrack* ChildTrack : ObjectEventsTrack->GetGameplayTrack().GetChildTracks())
		{
			if(ChildTrack->GetTimingTrack()->Is<FObjectEventsTrack>())
			{
				ChildTrack->SetIndent(ObjectEventsTrack->GetGameplayTrack().GetIndent() + 1);
				UpdateTrackOrderRecursive(StaticCastSharedRef<FObjectEventsTrack>(ChildTrack->GetTimingTrack()), InOutOrder);
			}
		}

		for(FGameplayTrack* ChildTrack : ObjectEventsTrack->GetGameplayTrack().GetChildTracks())
		{
			if(!ChildTrack->GetTimingTrack()->Is<FObjectEventsTrack>())
			{
				ChildTrack->SetIndent(ObjectEventsTrack->GetGameplayTrack().GetIndent() + 1);
				ChildTrack->GetTimingTrack()->SetOrder(InOutOrder++);
			}
		}
	}
}

void FGameplaySharedData::Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	AnalysisSession = &InAnalysisSession;

	const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(GetAnalysisSession());

		// Add a track for each tracked object
		GameplayProvider->EnumerateObjects([this, &InTimingViewSession, &InAnalysisSession, &GameplayProvider](const FObjectInfo& InObjectInfo)
		{
			GameplayProvider->ReadObjectEventsTimeline(InObjectInfo.Id, [this, &InTimingViewSession, &InAnalysisSession, &InObjectInfo](const IGameplayProvider::ObjectEventsTimeline& InTimeline)
			{
				if(InTimeline.GetEventCount() > 0)
				{
					GetObjectEventsTrackForId(InTimingViewSession, InAnalysisSession, InObjectInfo);
				}
			});
		});

		if(bObjectTracksDirty)
		{
			SortTracks();
			InTimingViewSession.InvalidateScrollableTracksOrder();
			bObjectTracksDirty = false;
		}
	}
}

void FGameplaySharedData::ExtendFilterMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.AddSubMenu(
		LOCTEXT("ToggleGameplayTracks", "Gameplay Tracks"),
		LOCTEXT("ToggleGameplayTracks_Tooltip", "Show/hide individual gameplay tracks"),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& InSubMenuBuilder)
		{ 
			InSubMenuBuilder.AddWidget(
				SNew(SBox)
				.MaxDesiredHeight(300.0f)
				.MinDesiredWidth(300.0f)
				[
					SNew(SGameplayTrackTree, *this)
				],
				FText(), true);
		})
	);

	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleEventTracks", "Event Tracks"),
		LOCTEXT("ToggleEventTracks_Tooltip", "Show/hide the gameplay event tracks"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGameplaySharedData::ToggleGameplayTracks),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FGameplaySharedData::AreGameplayTracksEnabled)),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);
}

void FGameplaySharedData::SortTracks()
{
	int32 Order = 10000;

	// Find current roots
	RootTracks.Reset();

	for(auto ObjectTrackPair : ObjectTracks)
	{
		if(ObjectTrackPair.Value->GetGameplayTrack().GetParentTrack() == nullptr)
		{
			RootTracks.Add(ObjectTrackPair.Value.ToSharedRef());
		}
	}

	// Sort roots alphabetically
	Algo::Sort(RootTracks, [](TSharedPtr<FBaseTimingTrack> InTrack0, TSharedPtr<FBaseTimingTrack> InTrack1)
	{
		return InTrack0->GetName() < InTrack1->GetName();
	});

	// update ordering/indent
	for(TSharedPtr<FBaseTimingTrack> RootTrack : RootTracks)
	{
		UpdateTrackOrderRecursive(RootTrack.ToSharedRef(), Order);
	}

	OnTracksChangedDelegate.Broadcast();
}

void FGameplaySharedData::ToggleGameplayTracks()
{
	bObjectTracksEnabled = !bObjectTracksEnabled;

	for(auto ObjectTrackPair : ObjectTracks)
	{
		ObjectTrackPair.Value->SetVisibilityFlag(bObjectTracksEnabled);
	}
}

bool FGameplaySharedData::AreGameplayTracksEnabled() const
{
	return bObjectTracksEnabled;
}

void FGameplaySharedData::EnumerateObjectTracks(TFunctionRef<void(const TSharedRef<FObjectEventsTrack>&)> InCallback) const
{
	for(const auto& TrackPair : ObjectTracks)
	{
		InCallback(TrackPair.Value.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE