// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "Serialization/Archive.h"

/**
 * Tracks an FName ID to a time value. Time will be context dependent, but usually
 * represents the total amount of time a specific action took (how long a package
 * took to load, how long an actor had queued bunches, etc.)
 *
 * Could have used a TPair, but this will make it more obvious what we're tracking.
 */
struct ENGINE_API FDelinquencyNameTimePair
{
public:

	FDelinquencyNameTimePair(FName InName, float InTimeSeconds) :
		Name(InName),
		TimeSeconds(InTimeSeconds)
	{
	}

	FName Name;
	float TimeSeconds;
};

struct ENGINE_API FDelinquencyKeyFuncs : public BaseKeyFuncs<FDelinquencyNameTimePair, FDelinquencyNameTimePair, false>
{
	static KeyInitType GetSetKey(ElementInitType Element)
	{
		return Element;
	}

	static bool Matches(KeyInitType LHS, KeyInitType RHS)
	{
		return LHS.Name == RHS.Name;
	}

	static uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key.Name);
	}
};

/**
 * Convenience type that can be used to tracks information about things that can result in prolonged
 * periods of apparent network inactivity, despite actually receiving traffic.
 *
 * The overall number of entries is expected to be small, but ultimately is left up to callers.
 */
struct ENGINE_API FDelinquencyAnalytics
{
public:

	explicit FDelinquencyAnalytics(const uint32 InNumberOfTopOffendersToTrack) :
		TotalTime(0.f),
		NumberOfTopOffendersToTrack(InNumberOfTopOffendersToTrack)
	{
		TopOffenders.Reserve(NumberOfTopOffendersToTrack);
	}

	FDelinquencyAnalytics(FDelinquencyAnalytics&& Other):
		TopOffenders(MoveTemp(Other.TopOffenders)),
		AllDelinquents(MoveTemp(Other.AllDelinquents)),
		TotalTime(Other.TotalTime),
		NumberOfTopOffendersToTrack(Other.NumberOfTopOffendersToTrack)
	{
		TopOffenders.Reserve(NumberOfTopOffendersToTrack);
	}

	FDelinquencyAnalytics(const FDelinquencyAnalytics&) = delete;
	const FDelinquencyAnalytics& operator=(const FDelinquencyAnalytics&) = delete;
	FDelinquencyAnalytics& operator=(FDelinquencyAnalytics&&) = default;

	void Emplace(FName Name, float TimeSeconds)
	{
		Add(FDelinquencyNameTimePair(Name, TimeSeconds));
	}

	/**
	 * Adds the event to the delinquency tracking, by accumulating its time into total time,
	 * and updating any existing events to choose the one with the highest time.
	 *
	 * When NumberOfTopOffendersToTrack == 0, we will just track the set of all events as well as the total time.
	 *
	 * When NumberOfTopOffendersToTrack > 0, we will track the set, total time, and also maintain sorted list
	 * (highest to lowest) of events that occurred.
	 *
	 * By setting NumberOfTopOffendersToTrack to 0, users can manage their own lists of "TopOffenders", or
	 * otherwise avoid the per add overhead of this tracking.
	 */
	void Add(FDelinquencyNameTimePair&& ToTrack)
	{
		struct FHelper
		{
			static bool Compare(const FDelinquencyNameTimePair& LHS, const FDelinquencyNameTimePair& RHS)
			{
				return LHS.TimeSeconds > RHS.TimeSeconds;
			}
		};

		// Regardless of whether or not this item has been seen before, there was a new entry
		// so we'll add that time to the total.
		TotalTime += ToTrack.TimeSeconds;

		// TODO: We might consider tracking the code below as totals instead of the max time for a single event.
		// For example, an Actor could end up queueing bunches several times within a reporting window, and each
		// of those events would add to the TotalTime, but below we will only have the time of the longest period
		// where bunches were queued.

		if (NumberOfTopOffendersToTrack == 0)
		{
			AllDelinquents.Emplace(MoveTemp(ToTrack));
		}
		else if (TopOffenders.Num() == 0)
		{
			TopOffenders.Add(ToTrack);
			AllDelinquents.Emplace(MoveTemp(ToTrack));
			return;
		}
		else
		{
			if (FDelinquencyNameTimePair* AlreadyTracked = AllDelinquents.Find(ToTrack))
			{
				// We found an entry, so check its time.
				if (AlreadyTracked->TimeSeconds >= ToTrack.TimeSeconds)
				{
					// We already have tracked a worse offense for this entry, so there's nothing more
					// we need to do.
					return;
				}
				else if (TopOffenders.Num() > 0)
				{
					const float LeastOffensiveTime = TopOffenders.Last().TimeSeconds;
					if (AlreadyTracked->TimeSeconds >= LeastOffensiveTime)
					{
						// Our previous offense would have been tracked in the TopOffenders list, so go ahead and remove it.
						// We're sorted highest to lowest, but are using greater than to actually compare, so UpperBound should
						// return the index above which of the entry just before us.
						// However, it's possible that other entries have the same time, so we need to make sure we remove the
						// correct one.
						int32 MaybeOurEntry = Algo::UpperBound<>(TopOffenders, ToTrack, &FHelper::Compare);
						while(true)
						{
							// Sanity check that we have actually found our entry.
							// If we hit the end of the list, or we see an entry that should be less offensive, then
							// we've missed our entry.
							if (MaybeOurEntry == TopOffenders.Num() || TopOffenders[MaybeOurEntry].TimeSeconds < AlreadyTracked->TimeSeconds)
							{
								// It's possible that multiple entries have the same delinquency time.
								// If our current entry matches the LeastOffensiveTime, it's possible that we were in the
								// top offenders list at one point but were pushed out when more offensive entries were added.
								// If our time doesn't match the least offensive time, we should definitely be in the list.
								ensureMsgf(AlreadyTracked->TimeSeconds == LeastOffensiveTime, TEXT("FDelinquencyAnalytics::Add - Unable to find expected entry %s:%f, list may not be sorted!"), *AlreadyTracked->Name.ToString(), AlreadyTracked->TimeSeconds);
								break;
							}

							// We found our entry, so we're done.
							else if (TopOffenders[MaybeOurEntry].Name == AlreadyTracked->Name)
							{
								TopOffenders.RemoveAt(MaybeOurEntry, 1, false);
								break;
							}

							++MaybeOurEntry;
						}
					}
				}
			}

			AllDelinquents.Add(ToTrack);

			const int32 LocalNumberOfTopOffendersToTrack = static_cast<int32>(NumberOfTopOffendersToTrack);
			const int32 InsertAt = Algo::UpperBound<>(TopOffenders, ToTrack, &FHelper::Compare);

			// Check to see if this time was ranked in our top offenders.
			if (InsertAt < LocalNumberOfTopOffendersToTrack)
			{
				// If we're going to displace a previous top offender, remove the least offensive.
				if (LocalNumberOfTopOffendersToTrack == TopOffenders.Num())
				{
					TopOffenders.RemoveAt(TopOffenders.Num() - 1, 1, false);
				}

				TopOffenders.InsertUninitialized(InsertAt, 1);
				TopOffenders[InsertAt] = MoveTemp(ToTrack);
			}
		}
	}

	const TArray<FDelinquencyNameTimePair>& GetTopOffenders() const
	{
		return TopOffenders;
	}

	const TSet<FDelinquencyNameTimePair, FDelinquencyKeyFuncs>& GetAllDelinquents() const
	{
		return AllDelinquents;
	}

	const float GetTotalTime() const
	{
		return TotalTime;
	}

	const uint32 GetNumberOfTopOffendersToTrack() const
	{
		return NumberOfTopOffendersToTrack;
	}

	void Reset()
	{
		TopOffenders.Reset();
		AllDelinquents.Reset();
		TotalTime = 0;
	}

	void CountBytes(FArchive& Ar) const
	{
		TopOffenders.CountBytes(Ar);
		AllDelinquents.CountBytes(Ar);
	}

private:

	TArray<FDelinquencyNameTimePair> TopOffenders;
	TSet<FDelinquencyNameTimePair, FDelinquencyKeyFuncs> AllDelinquents;
	float TotalTime;

	// This is explicitly non const, as we will be copying / moving these structs around.
	uint32 NumberOfTopOffendersToTrack;
};

/**
 * Tracks data related specific to a NetDriver that can can result in prolonged periods of apparent
 * network inactivity, despite actually receiving traffic.
 *
 * This includes things like Pending Async Loads.
 *
 * Also @see FConnectionDelinquencyAnalytics and FDelinquencyAnalytics.
 */
struct ENGINE_API FNetAsyncLoadDelinquencyAnalytics
{
	FNetAsyncLoadDelinquencyAnalytics() :
		DelinquentAsyncLoads(0),
		MaxConcurrentAsyncLoads(0)
	{
	}

	FNetAsyncLoadDelinquencyAnalytics(const uint32 NumberOfTopOffendersToTrack) :
		DelinquentAsyncLoads(NumberOfTopOffendersToTrack),
		MaxConcurrentAsyncLoads(0)
	{
	}

	FNetAsyncLoadDelinquencyAnalytics(FNetAsyncLoadDelinquencyAnalytics&& Other) :
		DelinquentAsyncLoads(MoveTemp(Other.DelinquentAsyncLoads)),
		MaxConcurrentAsyncLoads(Other.MaxConcurrentAsyncLoads)
	{
	}

	FNetAsyncLoadDelinquencyAnalytics(const FNetAsyncLoadDelinquencyAnalytics&) = delete;
	const FNetAsyncLoadDelinquencyAnalytics& operator=(const FNetAsyncLoadDelinquencyAnalytics&) = delete;
	FNetAsyncLoadDelinquencyAnalytics& operator=(FNetAsyncLoadDelinquencyAnalytics&&) = default;

	void CountBytes(FArchive& Ar) const
	{
		DelinquentAsyncLoads.CountBytes(Ar);
	}

	void Reset()
	{
		DelinquentAsyncLoads.Reset();
		MaxConcurrentAsyncLoads = 0;
	}

	FDelinquencyAnalytics DelinquentAsyncLoads;
	uint32 MaxConcurrentAsyncLoads;
};

/**
 * Tracks data related specific to a NetConnection that can can result in prolonged periods of apparent
 * network inactivity, despite actually receiving traffic.
 *
 * Also @see FDriverDelinquencyAnalytics and FDelinquencyAnalytics.
 */
struct ENGINE_API FNetQueuedActorDelinquencyAnalytics
{
	FNetQueuedActorDelinquencyAnalytics() :
		DelinquentQueuedActors(0),
		MaxConcurrentQueuedActors(0)
	{
	}

	FNetQueuedActorDelinquencyAnalytics(const uint32 NumberOfTopOffendersToTrack) :
		DelinquentQueuedActors(NumberOfTopOffendersToTrack),
		MaxConcurrentQueuedActors(0)
	{
	}

	FNetQueuedActorDelinquencyAnalytics(FNetQueuedActorDelinquencyAnalytics&& Other) :
		DelinquentQueuedActors(MoveTemp(Other.DelinquentQueuedActors)),
		MaxConcurrentQueuedActors(Other.MaxConcurrentQueuedActors)
	{
	}

	FNetQueuedActorDelinquencyAnalytics(const FNetQueuedActorDelinquencyAnalytics&) = delete;
	const FNetQueuedActorDelinquencyAnalytics& operator=(const FNetQueuedActorDelinquencyAnalytics&) = delete;
	FNetQueuedActorDelinquencyAnalytics& operator=(FNetQueuedActorDelinquencyAnalytics&&) = default;


	void CountBytes(FArchive& Ar) const
	{
		DelinquentQueuedActors.CountBytes(Ar);
	}

	void Reset()
	{
		DelinquentQueuedActors.Reset();
		MaxConcurrentQueuedActors = 0;
	}

	FDelinquencyAnalytics DelinquentQueuedActors;
	uint32 MaxConcurrentQueuedActors;
};