// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Insights/ViewModels/GraphTrack.h"
#include "Algo/Sort.h"

// Provides parent/child hierarchy structure and the owning object Id
// Designed as a compositional member of outer timing tracks
class FGameplayTrack
{
public:
	FGameplayTrack(FBaseTimingTrack& InTimingTrack, uint64 InObjectId)
		: TimingTrack(InTimingTrack)
		, ObjectId(InObjectId)
		, Parent(nullptr)
	{
	}

	// Get the object ID for this track
	uint64 GetObjectId() const { return ObjectId; }

	/** Get the parent track */
	FGameplayTrack* GetParentTrack() { return Parent; }

	/** Get child tracks */
	TArrayView<FGameplayTrack*> GetChildTracks() { return Children; }

	/** Add a child track */
	void AddChildTrack(FGameplayTrack& InChildTrack)
	{
		check(InChildTrack.Parent == nullptr);
		InChildTrack.Parent = this;

		Children.Add(&InChildTrack);

		Algo::Sort(Children, [](FGameplayTrack* InTrack0, FGameplayTrack* InTrack1)
		{
			return InTrack0->GetTimingTrack()->GetName() < InTrack1->GetTimingTrack()->GetName();
		});	
	}

	/** Find a child track using the specified callback */
	TSharedPtr<FBaseTimingTrack> FindChildTrack(uint64 InObjectId, TFunctionRef<bool(const FBaseTimingTrack& InTrack)> Callback)
	{
		for(FGameplayTrack* ChildTrack : Children)
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

	/** Access the outer timing track */
	TSharedRef<FBaseTimingTrack> GetTimingTrack() const { return TimingTrack.AsShared(); }

private:
	/** Outer timing track */
	FBaseTimingTrack& TimingTrack;

	// The object ID for this track
	uint64 ObjectId;

	/** Parent track */
	FGameplayTrack* Parent;

	/** Our child tracks */
	TArray<FGameplayTrack*> Children;
};

template <class Base>
class TGameplayTrackMixin : public Base
{
public:
	TGameplayTrackMixin(uint64 InObjectId, const FName& InType, const FName& InSubType, const FText& InName)
		: Base(InType, InSubType, InName.ToString())
		, GameplayTrack(*this, InObjectId)
	{
	}

	/** Access the underlying gameplay track */
	FGameplayTrack& GetGameplayTrack() { return GameplayTrack; }

	/** Access the underlying gameplay track */
	const FGameplayTrack& GetGameplayTrack() const { return GameplayTrack; }

private:
	FGameplayTrack GameplayTrack;
};