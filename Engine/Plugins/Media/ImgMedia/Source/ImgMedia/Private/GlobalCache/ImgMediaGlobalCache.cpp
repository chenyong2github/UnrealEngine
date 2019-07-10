// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ImgMediaGlobalCache.h"
#include "IImgMediaReader.h"
#include "ImgMediaPrivate.h"
#include "ImgMediaSettings.h"


FImgMediaGlobalCache::FImgMediaGlobalCache()
	: LeastRecent(nullptr)
	, MostRecent(nullptr)
	, CurrentSize(0)
	, MaxSize(0)
{
}

FImgMediaGlobalCache::~FImgMediaGlobalCache()
{
	Shutdown();
}

void FImgMediaGlobalCache::Initialize()
{
	auto Settings = GetDefault<UImgMediaSettings>();
	MaxSize = Settings->GlobalCacheSizeGB * 1024 * 1024 * 1024;
}

void FImgMediaGlobalCache::Shutdown()
{
	FScopeLock Lock(&CriticalSection);
	Empty();
}

void FImgMediaGlobalCache::AddFrame(const FName& Sequence, int32 Index, const TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe>& Frame)
{
	FScopeLock Lock(&CriticalSection);

	// Make sure we have enough space in the cache to add this new frame.
	SIZE_T FrameSize = Frame->Info.UncompressedSize;
	if (FrameSize <= MaxSize)
	{
		// Empty cache until we have enough space.
		while (CurrentSize + FrameSize > MaxSize)
		{
			FName* RemoveSequencePtr = MapLeastRecentToSequence.Find(LeastRecent);
			FName RemoveSequence = RemoveSequencePtr != nullptr ? *RemoveSequencePtr : FName();
			Remove(RemoveSequence, *LeastRecent);
		}

		// Create new entry.
		FImgMediaGlobalCacheEntry* NewEntry = new FImgMediaGlobalCacheEntry(Index, Frame);
		MapFrameToEntry.Emplace(TPair<FName, int32>(Sequence, Index), NewEntry);
	
		MarkAsRecent(Sequence, *NewEntry);

		CurrentSize += FrameSize;
	}
	else
	{
		UE_LOG(LogImgMedia, Warning, TEXT("Global cache size %d is smaller than frame size %d."), MaxSize, FrameSize);
	}
}

TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe>* FImgMediaGlobalCache::FindAndTouch(const FName& Sequence, int32 Index)
{
	FScopeLock Lock(&CriticalSection);

	TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe>* Frame = nullptr;

	FImgMediaGlobalCacheEntry** Entry = MapFrameToEntry.Find(TPair<FName, int32>(Sequence, Index));

	if ((Entry != nullptr) && (*Entry != nullptr))
	{
		Frame = &((*Entry)->Frame);

		// Mark this as the most recent.
		Unlink(Sequence, **Entry);
		MarkAsRecent(Sequence, **Entry);
	}

	return Frame;
}

void FImgMediaGlobalCache::GetIndices(const FName& Sequence, TArray<int32>& OutIndices) const
{
	FScopeLock Lock(&CriticalSection);

	// Get most recent entry in this sequence.
	FImgMediaGlobalCacheEntry* const* CurrentPtr = MapSequenceToMostRecentEntry.Find(Sequence);
	const FImgMediaGlobalCacheEntry* Current = CurrentPtr != nullptr ? *CurrentPtr : nullptr;

	// Loop over all entries in the sequence.
	while (Current != nullptr)
	{
		OutIndices.Add(Current->Index);
		Current = Current->LessRecentSequence;
	}
}

void FImgMediaGlobalCache::Remove(const FName& Sequence, FImgMediaGlobalCacheEntry& Entry)
{
	// Remove from cache.
	Unlink(Sequence, Entry);

	// Update current cache size.
	SIZE_T FrameSize = Entry.Frame->Info.UncompressedSize;
	CurrentSize -= FrameSize;

	// Delete entry.
	MapFrameToEntry.Remove(TPair<FName, int32>(Sequence, Entry.Index));
	delete &Entry;
}

void FImgMediaGlobalCache::MarkAsRecent(const FName& Sequence, FImgMediaGlobalCacheEntry& Entry)
{
	// Mark most recent.
	Entry.LessRecent = MostRecent;
	if (MostRecent != nullptr)
	{
		MostRecent->MoreRecent = &Entry;
	}
	MostRecent = &Entry;

	// Mark most recent in sequence.
	FImgMediaGlobalCacheEntry** SequenceMostRecentPtr = MapSequenceToMostRecentEntry.Find(Sequence);
	FImgMediaGlobalCacheEntry* SequenceMostRecent = (SequenceMostRecentPtr != nullptr) ? (*SequenceMostRecentPtr) : nullptr;
	Entry.LessRecentSequence = SequenceMostRecent;
	if (SequenceMostRecent != nullptr)
	{
		SequenceMostRecent->MoreRecentSequence = &Entry;
	}
	else
	{
		// If we did not have a most recent one, then this is the first in the sequence.
		MapLeastRecentToSequence.Emplace(&Entry, Sequence);
	}
	MapSequenceToMostRecentEntry.Emplace(Sequence, &Entry);

	// If LeastRecent is null, then set it now.
	if (LeastRecent == nullptr)
	{
		LeastRecent = &Entry;
	}
}

void FImgMediaGlobalCache::Unlink(const FName& Sequence, FImgMediaGlobalCacheEntry& Entry)
{
	// Remove from link.
	if (Entry.LessRecent != nullptr)
	{
		Entry.LessRecent->MoreRecent = Entry.MoreRecent;
	}
	else if (LeastRecent == &Entry)
	{
		LeastRecent = Entry.MoreRecent;
	}

	if (Entry.MoreRecent != nullptr)
	{
		Entry.MoreRecent->LessRecent = Entry.LessRecent;
	}
	else if (MostRecent == &Entry)
	{
		MostRecent = Entry.LessRecent;
	}

	Entry.LessRecent = nullptr;
	Entry.MoreRecent = nullptr;

	// Remove from sequence link.
	if (Entry.LessRecentSequence != nullptr)
	{
		Entry.LessRecentSequence->MoreRecentSequence = Entry.MoreRecentSequence;
	}
	else
	{
		MapLeastRecentToSequence.Remove(&Entry);
		if (Entry.MoreRecentSequence != nullptr)
		{
			MapLeastRecentToSequence.Emplace(Entry.MoreRecentSequence, Sequence);
		}
	}

	if (Entry.MoreRecentSequence != nullptr)
	{
		Entry.MoreRecentSequence->LessRecentSequence = Entry.LessRecentSequence;
	}
	else
	{
		// Update most recent in sequence.
		FImgMediaGlobalCacheEntry** MostRecentSequence = MapSequenceToMostRecentEntry.Find(Sequence);
		if (MostRecentSequence != nullptr)
		{
			if (*MostRecentSequence == &Entry)
			{
				MapSequenceToMostRecentEntry.Emplace(Sequence, Entry.LessRecentSequence);
			}
		}
	}

	Entry.LessRecent = nullptr;
	Entry.LessRecentSequence = nullptr;
	Entry.MoreRecent = nullptr;
	Entry.MoreRecentSequence = nullptr;
}

void FImgMediaGlobalCache::Empty()
{
	while (LeastRecent != nullptr)
	{
		FImgMediaGlobalCacheEntry *Entry = LeastRecent;
		LeastRecent = Entry->MoreRecent;

		delete Entry;
	}

	MostRecent = nullptr;
	CurrentSize = 0;

	MapSequenceToMostRecentEntry.Empty();
	MapLeastRecentToSequence.Empty();
	MapFrameToEntry.Empty();
}
