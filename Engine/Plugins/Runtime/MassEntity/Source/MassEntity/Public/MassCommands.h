// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/CsvProfilerConfig.h"
#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"

#define WITH_MASS_BATCHED_COMMANDS 1

struct FMassBatchedCommand
{
	virtual ~FMassBatchedCommand() {}

	virtual void Execute(UMassEntitySubsystem& System) const = 0;
	virtual void Reset()
	{
		UE_MT_SCOPED_WRITE_ACCESS(EntitiesAccessDetector);
		TargetEntities.Reset();
	}

	void Add(FMassEntityHandle Entity)
	{
		UE_MT_SCOPED_WRITE_ACCESS(EntitiesAccessDetector);
		TargetEntities.Add(Entity);
	}

	bool HasWork() const 
	{ 
		UE_MT_SCOPED_READ_ACCESS(EntitiesAccessDetector);
		return TargetEntities.Num() > 0; 
	}

	template<typename T>
	FORCENOINLINE static uint32 GetCommandIndex()
	{
		static const uint32 ThisTypesStaticIndex = CommandsCounter++;
		return ThisTypesStaticIndex;
	}

	virtual SIZE_T GetAllocatedSize() const
	{
		return TargetEntities.GetAllocatedSize();
	}

#if CSV_PROFILER || WITH_MASSENTITY_DEBUG
	int32 GetNumEntitiesStat() const  { return TargetEntities.Num(); }
	FName GetFName() const { return DebugName; }
#endif // CSV_PROFILER || WITH_MASSENTITY_DEBUG

protected:
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(EntitiesAccessDetector); 
	TArray<FMassEntityHandle> TargetEntities;
#if CSV_PROFILER || WITH_MASSENTITY_DEBUG
	FName DebugName;
#endif // CSV_PROFILER || WITH_MASSENTITY_DEBUG
private:
	MASSENTITY_API static std::atomic<uint32> CommandsCounter;
};

struct FMassBatchedCommandChangeTags : public FMassBatchedCommand
{
protected:
	virtual void Execute(UMassEntitySubsystem& System) const override
	{
		TArray<FMassArchetypeSubChunks> ChunkCollections;
		UE::Mass::Utils::CreateSparseChunks(System, TargetEntities, FMassArchetypeSubChunks::FoldDuplicates, ChunkCollections);

		System.BatchChangeTagsForEntities(ChunkCollections, TagsToAdd, TagsToRemove);
	}

	virtual SIZE_T GetAllocatedSize() const override
	{
		return TagsToAdd.GetAllocatedSize() + TagsToRemove.GetAllocatedSize() + FMassBatchedCommand::GetAllocatedSize();
	}

	FMassTagBitSet TagsToAdd;
	FMassTagBitSet TagsToRemove;
};

template<typename T>
struct FCommandAddTag : public FMassBatchedCommandChangeTags
{
	FCommandAddTag()
	{
		TagsToAdd.Add<T>();
#if CSV_PROFILER || WITH_MASSENTITY_DEBUG
		DebugName = TEXT("CommandAddTag");
#endif // CSV_PROFILER || WITH_MASSENTITY_DEBUG
	}
};

template<typename T>
struct FCommandRemoveTag : public FMassBatchedCommandChangeTags
{
	FCommandRemoveTag()
	{
		TagsToRemove.Add<T>();
#if CSV_PROFILER || WITH_MASSENTITY_DEBUG
		DebugName = TEXT("RemoveAddTag");
#endif // CSV_PROFILER || WITH_MASSENTITY_DEBUG
	}
};

template<typename TOld, typename TNew>
struct FCommandSwapTags : public FMassBatchedCommandChangeTags
{
	FCommandSwapTags()
	{
		TagsToRemove.Add<TOld>();
		TagsToAdd.Add<TNew>();
#if CSV_PROFILER || WITH_MASSENTITY_DEBUG
		DebugName = TEXT("SwapAddTags");
#endif // CSV_PROFILER || WITH_MASSENTITY_DEBUG
	}
};
