// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveUObject.h"

struct FPropertySelection;
struct FLevelSnapshot_Property;
struct FBaseObjectInfo;

/* Serializes snapshot objects given related FBaseObjectInfo info. */
class FObjectSnapshotArchive : public FArchiveUObject
{
	using Super = FArchiveUObject;

	// Internal Helper class
	struct FPropertyInfo
	{
		FName PropertyPath;
		FProperty* Property;
		uint32 PropertyDepth;
	};

	union FObjectInfoUnion
	{
		FBaseObjectInfo* ObjectInfo;
		const FBaseObjectInfo* ConstObjectInfo;
	};

protected:

	FObjectSnapshotArchive() = default;

	void SetWritableObjectInfo(FBaseObjectInfo& InObjectInfo);
	void SetReadOnlyObjectInfo(const FBaseObjectInfo& InObjectInfo);

public:

	//~ Begin FArchive Interface
	virtual FString GetArchiveName() const override;
	virtual int64 TotalSize() override;
	virtual int64 Tell() override;
	virtual void Seek(int64 InPos) override;

	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	virtual void Serialize(void* Data, int64 Num) override;

	virtual FArchive& operator<<(class FName& Name) override;
	virtual FArchive& operator<<(class UObject*& Object) override;
	//~ Begin FArchive Interface

private:
	
	FLevelSnapshot_Property& GetProperty();
	FPropertyInfo BuildPropertyInfo();

	const FLevelSnapshot_Property* FindMatchingProperty() const;

	FObjectInfoUnion ObjectInfoUnion;
	int64 Offset = 0;

	static uint64 RequiredPropertyFlags;
	static uint64 ExcludedPropertyFlags;
};

