// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyCache.generated.h"

struct FRigHierarchyContainer;
struct FRigBoneHierarchy;
struct FRigSpaceHierarchy;
struct FRigControlHierarchy;
struct FRigCurveContainer;

USTRUCT(BlueprintType)
struct FCachedRigElement
{
	GENERATED_BODY()

public:

	FORCEINLINE FCachedRigElement()
		: Key()
		, Index(UINT16_MAX)
		, ContainerVersion(INDEX_NONE)
	{}

	FORCEINLINE FCachedRigElement(const FRigElementKey& InKey, const FRigHierarchyContainer* InContainer)
		: Key()
		, Index(UINT16_MAX)
		, ContainerVersion(INDEX_NONE)
	{
		UpdateCache(InKey, InContainer);
	}

	FORCEINLINE FCachedRigElement(const FName& InName, const FRigBoneHierarchy* InHierarchy)
		: Key()
		, Index(UINT16_MAX)
		, ContainerVersion(INDEX_NONE)
	{
		UpdateCache(InName, InHierarchy);
	}

	FORCEINLINE FCachedRigElement(const FName& InName, const FRigSpaceHierarchy* InHierarchy)
		: Key()
		, Index(UINT16_MAX)
		, ContainerVersion(INDEX_NONE)
	{
		UpdateCache(InName, InHierarchy);
	}

	FORCEINLINE FCachedRigElement(const FName& InName, const FRigControlHierarchy* InHierarchy)
		: Key()
		, Index(UINT16_MAX)
		, ContainerVersion(INDEX_NONE)
	{
		UpdateCache(InName, InHierarchy);
	}

	FORCEINLINE FCachedRigElement(const FName& InName, const FRigCurveContainer* InHierarchy)
		: Key()
		, Index(UINT16_MAX)
		, ContainerVersion(INDEX_NONE)
	{
		UpdateCache(InName, InHierarchy);
	}

	FORCEINLINE bool IsValid() const
	{
		return GetIndex() != INDEX_NONE && Key.IsValid();
	}

	FORCEINLINE void Reset()
	{
		Key = FRigElementKey();
		Index = UINT16_MAX;
		ContainerVersion = INDEX_NONE;
	}

	FORCEINLINE operator bool() const
	{
		return IsValid();
	}

	FORCEINLINE int32 GetIndex() const
	{
		if(Index == UINT16_MAX)
		{
			return INDEX_NONE;
		}
		return (int32)Index;
	}

	FORCEINLINE const FRigElementKey& GetKey() const
	{
		return Key;
	}

	bool UpdateCache(const FRigHierarchyContainer* InContainer);

	bool UpdateCache(const FRigElementKey& InKey, const FRigHierarchyContainer* InContainer);

	bool UpdateCache(const FName& InName, const FRigBoneHierarchy* InHierarchy);

	bool UpdateCache(const FName& InName, const FRigSpaceHierarchy* InHierarchy);

	bool UpdateCache(const FName& InName, const FRigControlHierarchy* InHierarchy);

	bool UpdateCache(const FName& InName, const FRigCurveContainer* InHierarchy);

	friend FORCEINLINE uint32 GetTypeHash(const FCachedRigElement& Cache)
	{
		return GetTypeHash(Cache.Key) * 13 + (uint32)Cache.Index;
	}

	bool IsIdentical(const FRigElementKey& InKey, const FRigHierarchyContainer* InContainer);

	FORCEINLINE bool operator ==(const FCachedRigElement& Other) const
	{
		return Index == Other.Index && Key == Other.Key;
	}

	FORCEINLINE bool operator !=(const FCachedRigElement& Other) const
	{
		return Index != Other.Index || Key != Other.Key;
	}

	FORCEINLINE bool operator ==(const FRigElementKey& Other) const
	{
		return Key == Other;
	}

	FORCEINLINE bool operator !=(const FRigElementKey& Other) const
	{
		return Key != Other;
	}

	FORCEINLINE bool operator ==(const int32& Other) const
	{
		return GetIndex() == Other;
	}

	FORCEINLINE bool operator !=(const int32& Other) const
	{
		return GetIndex() != Other;
	}

	FORCEINLINE bool operator <(const FCachedRigElement& Other) const
	{
		if (Key < Other.Key)
		{
			return true;
		}
		return Index < Other.Index;
	}

	FORCEINLINE bool operator >(const FCachedRigElement& Other) const
	{
		if (Key > Other.Key)
		{
			return true;
		}
		return Index > Other.Index;
	}

	FORCEINLINE operator int32() const
	{
		return GetIndex();
	}

	FORCEINLINE operator FRigElementKey() const
	{
		return Key;
	}

private:

	UPROPERTY()
	FRigElementKey Key;

	UPROPERTY()
	uint16 Index;

	UPROPERTY()
	int32 ContainerVersion;
};
