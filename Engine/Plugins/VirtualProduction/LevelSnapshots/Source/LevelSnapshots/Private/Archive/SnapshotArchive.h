// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveUObject.h"

struct FObjectSnapshotData;
struct FWorldSnapshotData;

/* Handles shared logic for saving and loading data for snapshots. */
class FSnapshotArchive : public FArchiveUObject
{
	using Super = FArchiveUObject;
	
public:

	static FSnapshotArchive MakeArchiveForRestoring(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData);

	//~ Begin FArchive Interface
	virtual FString GetArchiveName() const override;
	virtual int64 TotalSize() override;
	virtual int64 Tell() override;
	virtual void Seek(int64 InPos) override;
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	
	virtual FArchive& operator<<(FName& Value) override;
	virtual FArchive& operator<<(UObject*& Value) override;
	virtual void Serialize(void* Data, int64 Length) override;
	//~ End FArchive Interface

protected:

	FWorldSnapshotData& GetSharedData() const
	{
		return SharedData;
	}
	
	/* Only used when Loading.
	 * True: we're loading into a temp world. False: we're loading into an editor world.
	 * 
	 * If true, object references within the world are translated to use the temp world package.
	 */
	bool bShouldLoadObjectDependenciesForTempWorld = true;

	int32 ExcludedPropertyFlags;

	FSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading);
	
private:
	
	/*  Where in ObjectData we're currently writing to. */
	int64 DataIndex = 0;

	/* The object's serialized data */
	FObjectSnapshotData& ObjectData;
	/* Stores shared data, e.g. FNames and FSoftObjectPaths */
	FWorldSnapshotData& SharedData;
	
};
