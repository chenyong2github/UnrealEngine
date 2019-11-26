// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplaySharedData.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "ObjectEventsTrack.h"
#include "Algo/Sort.h"
#include "GameplayProvider.h"
#include "Insights/ITimingViewSession.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

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

static void UpdateTrackOrderRecursive(TSharedRef<FObjectEventsTrack> InTrack, int32& InOutOrder, uint32 InDepth)
{
	// recurse down object-track children, then non-object track leaf tracks to set 
	// overall ordering based on depth-first traversal of the hierarchy

	InTrack->SetOrder(InOutOrder++);
	InTrack->GetGameplayTrack().SetIndent(InDepth);

	for(FGameplayTrack* ChildTrack : InTrack->GetGameplayTrack().GetChildTracks())
	{
		if(ChildTrack->GetTimingTrack()->GetType() == FObjectEventsTrack::TypeName)
		{
			UpdateTrackOrderRecursive(StaticCastSharedRef<FObjectEventsTrack>(ChildTrack->GetTimingTrack()), InOutOrder, InDepth + 1);
		}
	}

	for(FGameplayTrack* ChildTrack : InTrack->GetGameplayTrack().GetChildTracks())
	{
		if(ChildTrack->GetTimingTrack()->GetType() != FObjectEventsTrack::TypeName)
		{
			ChildTrack->GetTimingTrack()->SetOrder(InOutOrder++);
			ChildTrack->SetIndent(InDepth + 1);
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
			TSharedPtr<FObjectEventsTrack> ObjectEventsTrack = GetObjectEventsTrackForId(InTimingViewSession, InAnalysisSession, InObjectInfo);
			ObjectEventsTrack->SetVisibilityFlag(false);

			GameplayProvider->ReadObjectEventsTimeline(InObjectInfo.Id, [this, &ObjectEventsTrack](const IGameplayProvider::ObjectEventsTimeline& InTimeline)
			{
				ObjectEventsTrack->SetVisibilityFlag(bObjectTracksEnabled);
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
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("ToggleGameplayTracks", "Gameplay Tracks"),
		LOCTEXT("ToggleGameplayTracks_Tooltip", "Show/hide the gameplay tracks"),
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
	static TArray<TSharedRef<FObjectEventsTrack>> Roots;
	check(Roots.Num() == 0);

	for(auto ObjectTrackPair : ObjectTracks)
	{
		if(ObjectTrackPair.Value->GetGameplayTrack().GetParentTrack() == nullptr)
		{
			Roots.Add(ObjectTrackPair.Value.ToSharedRef());
		}
	}

	// Sort roots alphabetically
	Algo::Sort(Roots, [](TSharedRef<FObjectEventsTrack> InTrack0, TSharedRef<FObjectEventsTrack> InTrack1)
	{
		return InTrack0->GetName() < InTrack1->GetName();
	});

	// update ordering
	for(TSharedRef<FObjectEventsTrack> RootTrack : Roots)
	{
		UpdateTrackOrderRecursive(RootTrack, Order, 0);
	}

	Roots.Reset();
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