// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplaySharedData.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "ObjectEventsTrack.h"
#include "Algo/Sort.h"

#define LOCTEXT_NAMESPACE "GameplaySharedData"

FGameplaySharedData::FGameplaySharedData()
	: AnalysisSession(nullptr)
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

FObjectEventsTrack* FGameplaySharedData::GetObjectEventsTrackForId(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession, const FObjectInfo& InObjectInfo)
{
	const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	check(GameplayProvider);

	FObjectEventsTrack* LeafObjectEventsTrack = ObjectTracks.FindRef(InObjectInfo.Id);
	if(LeafObjectEventsTrack == nullptr)
	{
		LeafObjectEventsTrack = new FObjectEventsTrack(*this, InObjectInfo.Id, InObjectInfo.Name);
		LeafObjectEventsTrack->SetVisibilityFlag(bObjectTracksEnabled);
		ObjectTracks.Add(InObjectInfo.Id, LeafObjectEventsTrack);

		InTimingViewSession.AddTimingEventsTrack(LeafObjectEventsTrack);
	}

	// Fill the outer chain
	if(InObjectInfo.OuterId != 0)
	{
		FObjectEventsTrack* ObjectEventsTrack = LeafObjectEventsTrack;
		uint64 OuterId = InObjectInfo.OuterId;
		while(const FObjectInfo* OuterInfo = GameplayProvider->FindObjectInfo(OuterId))
		{
			FObjectEventsTrack* OuterEventsTrack = ObjectTracks.FindRef(OuterId);
			if(OuterEventsTrack == nullptr)
			{
				OuterEventsTrack = new FObjectEventsTrack(*this, OuterId, OuterInfo->Name);
				OuterEventsTrack->SetVisibilityFlag(bObjectTracksEnabled);
				ObjectTracks.Add(OuterId, OuterEventsTrack);

				InTimingViewSession.AddTimingEventsTrack(OuterEventsTrack);
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

	return LeafObjectEventsTrack;
}

static void UpdateTrackOrderRecursive(FObjectEventsTrack* InTrack, int32& InOutOrder)
{
	// recurse down object-track children, then non-object track leaf tracks to set 
	// overall ordering based on depth-first traversal of the hierarchy

	InTrack->SetOrder(InOutOrder++);

	for(FGameplayTrack* ChildTrack : InTrack->GetGameplayTrack().GetChildTracks())
	{
		if(ChildTrack->GetTimingTrack().GetType() == FObjectEventsTrack::TypeName)
		{
			UpdateTrackOrderRecursive(static_cast<FObjectEventsTrack*>(&ChildTrack->GetTimingTrack()), InOutOrder);
		}
	}

	for(FGameplayTrack* ChildTrack : InTrack->GetGameplayTrack().GetChildTracks())
	{
		if(ChildTrack->GetTimingTrack().GetType() != FObjectEventsTrack::TypeName)
		{
			ChildTrack->GetTimingTrack().SetOrder(InOutOrder++);
		}
	}
}

void FGameplaySharedData::Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	AnalysisSession = &InAnalysisSession;

	const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(GameplayProvider)
	{
		// Add a track for each tracked object
		GameplayProvider->EnumerateObjects([this, &InTimingViewSession, &InAnalysisSession, &GameplayProvider](const FObjectInfo& InObjectInfo)
		{
			FObjectEventsTrack* ObjectEventsTrack = GetObjectEventsTrackForId(InTimingViewSession, InAnalysisSession, InObjectInfo);
			ObjectEventsTrack->SetVisibilityFlag(false);

			GameplayProvider->ReadObjectEventsTimeline(InObjectInfo.Id, [this, &ObjectEventsTrack](const IGameplayProvider::ObjectEventsTimeline& InTimeline)
			{
				ObjectEventsTrack->SetVisibilityFlag(bObjectTracksEnabled);
			});
		});
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

void FGameplaySharedData::OnTracksChanged(int32& InOutOrder)
{
	// Find current roots
	static TArray<FObjectEventsTrack*> Roots;
	check(Roots.Num() == 0);

	for(auto ObjectTrackPair : ObjectTracks)
	{
		if(ObjectTrackPair.Value->GetGameplayTrack().GetParentTrack() == nullptr)
		{
			Roots.Add(ObjectTrackPair.Value);
		}
	}

	// Sort roots alphabetically
	Algo::Sort(Roots, [](FObjectEventsTrack* InTrack0, FObjectEventsTrack* InTrack1)
	{
		return InTrack0->GetName() < InTrack1->GetName();
	});

	// update ordering
	for(FObjectEventsTrack* RootTrack : Roots)
	{
		UpdateTrackOrderRecursive(RootTrack, InOutOrder);
	}

	Roots.Reset();
}

void FGameplaySharedData::ToggleGameplayTracks()
{
	bObjectTracksEnabled = !bObjectTracksEnabled;

	for(TPair<uint64, FObjectEventsTrack*> TrackPair : ObjectTracks)
	{
		TrackPair.Value->SetVisibilityFlag(bObjectTracksEnabled);
	}
}

bool FGameplaySharedData::AreGameplayTracksEnabled() const
{
	return bObjectTracksEnabled;
}

#undef LOCTEXT_NAMESPACE