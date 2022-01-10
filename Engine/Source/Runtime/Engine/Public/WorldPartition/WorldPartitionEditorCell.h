// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/HashBuilder.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartitionEditorCell.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartition;

/**
 * Represents an editing cell (editor-only)
 */
UCLASS(Within = WorldPartitionEditorHash)
class UWorldPartitionEditorCell: public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	//~ UObject interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ UObject interface

	template<typename Type>
	struct TActorHandle
	{
		inline TActorHandle(const Type& InHandle)
			: Source(InHandle->GetGuid())
			, Handle(InHandle)
		{}

		inline TActorHandle(const FGuid& InSource, const Type& InHandle)
			: Source(InSource)
			, Handle(InHandle)
		{}

		inline FWorldPartitionActorDesc* operator->() const
		{
			return *Handle;
		}

		inline FWorldPartitionActorDesc* operator*() const
		{
			return *Handle;
		}

		inline bool IsValid() const
		{
			return Handle.IsValid();
		}

		inline bool operator==(const TActorHandle& Other) const
		{
			return (Source == Other.Source) && (Handle == Other.Handle);
		}

		friend ENGINE_API inline uint32 GetTypeHash(const TActorHandle& ActorHandle)
		{
			FHashBuilder HashBuilder;
			HashBuilder << ActorHandle.Source << ActorHandle.Handle;
			return HashBuilder.GetHash();
		}

		FGuid Source;
		Type Handle;
	};

	typedef TActorHandle<FWorldPartitionHandle> FActorHandle;
	typedef TActorHandle<FWorldPartitionReference> FActorReference;

	void AddActor(const FWorldPartitionHandle& ActorHandle);
	void AddActor(const FGuid& Source, const FWorldPartitionHandle& ActorHandle);
	void RemoveActor(const FWorldPartitionHandle& ActorHandle);
	void RemoveActor(const FGuid& Source, const FWorldPartitionHandle& ActorHandle);

	inline bool IsLoaded() const
	{
		return bLoaded;
	}

	inline void SetLoaded(bool bInLoaded, bool bInLoadedByUserOperation)
	{
		bLoaded = bInLoaded;
		bLoadedChangedByUserOperation |= bInLoadedByUserOperation;
	}

	inline bool IsLoadedChangedByUserOperation() const
	{
		return bLoadedChangedByUserOperation;
	}

	inline bool IsEmpty() const
	{
		return !Actors.Num();
	}

	FBox Bounds;

private:
	/** Tells if the cell is loaded in the editor */
	bool bLoaded : 1;

	/** Tells if the cell loading state was changed by a user operation */
	bool bLoadedChangedByUserOperation : 1;

public:

	TSet<FActorHandle> Actors;
	TSet<FActorReference> LoadedActors;
#endif
};