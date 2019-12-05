// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"

// Provides parent/child hierarchy structure and the owning object Id
// Designed as a compositional member of outer timing tracks
class FGameplayTrack
{
public:
	FGameplayTrack(FBaseTimingTrack& InTimingTrack, uint64 InObjectId)
		: TimingTrack(InTimingTrack)
		, ObjectId(InObjectId)
		, Parent(nullptr)
		, Indent(0)
	{
	}

	// Get the object ID for this track
	uint64 GetObjectId() const { return ObjectId; }

	/** Get the parent track */
	FGameplayTrack* GetParentTrack() { return Parent; }

	/** Get child tracks */
	TArrayView<FGameplayTrack*> GetChildTracks() { return Children; }

	/** Add a child track */
	void AddChildTrack(FGameplayTrack& InChildTrack);

	/** Find a child track using the specified callback */
	TSharedPtr<FBaseTimingTrack> FindChildTrack(uint64 InObjectId, TFunctionRef<bool(const FBaseTimingTrack& InTrack)> Callback) const;

	/** Helper to draw the name for a timing track (uses indentation etc.) */
	void DrawHeaderForTimingTrack(const ITimingTrackDrawContext& InContext, const FBaseTimingTrack& InTrack, bool bUsePreallocatedLayers) const;

	/** Access the outer timing track */
	TSharedRef<FBaseTimingTrack> GetTimingTrack() const { return TimingTrack.AsShared(); }

	/** Access indent */
	void SetIndent(uint32 InIndent) { Indent = InIndent; }

	/** Access indent */
	uint32 GetIndent() const { return Indent; }

private:
	/** Outer timing track */
	FBaseTimingTrack& TimingTrack;

	// The object ID for this track
	uint64 ObjectId;

	/** Parent track */
	FGameplayTrack* Parent;

	/** Our child tracks */
	TArray<FGameplayTrack*> Children;

	/** Our indent when drawn in a tree */
	uint32 Indent;
};

template <class Base>
class TGameplayTrackMixin : public Base
{
public:
	TGameplayTrackMixin(uint64 InObjectId, const FName& InSubType, const FText& InName)
		: Base(InSubType, InName.ToString())
		, GameplayTrack(*this, InObjectId)
	{
	}

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