// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FArchive;
class FStructuredArchiveSlot;

class FPackageId
{
	static constexpr uint32 InvalidId = ~uint32(0u);
	uint32 Id = InvalidId;

	inline explicit FPackageId(int32 InId): Id(InId) {}

public:
	FPackageId() = default;

	inline static FPackageId FromIndex(uint32 Index)
	{
		return FPackageId(Index);
	}

	inline bool IsValid() const
	{
		return Id != InvalidId;
	}

	inline uint32 ToIndex() const
	{
		check(Id != InvalidId);
		return Id;
	}

	inline bool operator==(FPackageId Other) const
	{
		return Id == Other.Id;
	}

	inline friend uint32 GetTypeHash(const FPackageId& In)
	{
		return In.Id;
	}

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FPackageId& Value);

	COREUOBJECT_API friend void operator<<(FStructuredArchiveSlot Slot, FPackageId& Value);
};

