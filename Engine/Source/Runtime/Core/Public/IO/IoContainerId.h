// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FArchive;
class FStructuredArchiveSlot;

/**
 * Container ID.
 */
class FIoContainerId
{
public:
	inline FIoContainerId() = default;
	inline FIoContainerId(const FIoContainerId& Other) = default;
	inline FIoContainerId(FIoContainerId&& Other) = default;
	inline FIoContainerId& operator=(const FIoContainerId& Other) = default;

	static FIoContainerId FromIndex(const uint16 InIndex)
	{
		return FIoContainerId(InIndex);
	}

	inline bool IsValid() const
	{ 
		return Id != InvalidId;
	}

	inline uint16 ToIndex() const
	{
		check(Id != InvalidId);
		return Id;
	}

	inline bool operator<(FIoContainerId Other) const
	{
		return Id < Other.Id;
	}

	inline bool operator==(FIoContainerId Other) const
	{
		return Id == Other.Id;
	}

	inline bool operator!=(FIoContainerId Other) const
	{
		return Id != Other.Id;
	}

	inline friend uint32 GetTypeHash(const FIoContainerId& In)
	{
		return In.Id;
	}

	CORE_API friend FArchive& operator<<(FArchive& Ar, FIoContainerId& ContainerId);

	CORE_API friend void operator<<(FStructuredArchiveSlot Slot, FIoContainerId& Value);

private:
	inline explicit FIoContainerId(const uint16 InId)
		: Id(InId) { }

	static constexpr uint16 InvalidId = ~uint16(0u);

	uint16 Id = InvalidId;
};

