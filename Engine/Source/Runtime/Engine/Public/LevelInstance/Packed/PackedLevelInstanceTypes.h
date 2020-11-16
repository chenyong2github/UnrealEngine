// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Crc.h"
#include "Logging/LogMacros.h"

using FLevelInstancePackerID = uint32;

class ENGINE_API FLevelInstancePackerCluster
{
	FLevelInstancePackerID PackerID;

public:
	FLevelInstancePackerCluster(FLevelInstancePackerID InPackerID)
		: PackerID(InPackerID) {}
	virtual ~FLevelInstancePackerCluster() {}

	FLevelInstancePackerID GetPackerID() const { return PackerID; }

	virtual uint32 ComputeHash() const
	{
		return FCrc::TypeCrc32(PackerID);
	}

	virtual bool operator==(const FLevelInstancePackerCluster& Other) const
	{
		return PackerID == Other.PackerID;
	}

private:
	FLevelInstancePackerCluster(const FLevelInstancePackerCluster&) = delete;
	FLevelInstancePackerCluster& operator=(const FLevelInstancePackerCluster&) = delete;
};

class ENGINE_API FLevelInstancePackerClusterID
{
public:
	static FLevelInstancePackerClusterID Invalid;

	FLevelInstancePackerClusterID() : Hash(0)
	{
	}

	FLevelInstancePackerClusterID(FLevelInstancePackerClusterID&& Other)
		: Hash(Other.Hash), Data(MoveTemp(Other.Data))
	{
	}

	FLevelInstancePackerClusterID(TUniquePtr<FLevelInstancePackerCluster>&& InData)
		: Data(MoveTemp(InData))
	{
		Hash = Data->ComputeHash();
	}
		
	bool operator==(const FLevelInstancePackerClusterID& Other) const
	{
		return (Hash == Other.Hash) && (*Data == *Other.Data);
	}

	bool operator!=(const FLevelInstancePackerClusterID& Other) const
	{
		return !(*this == Other);
	}

	uint32 GetHash() const { return Hash; }

	FLevelInstancePackerID GetPackerID() const { return Data->GetPackerID(); }
		
	friend uint32 GetTypeHash(const FLevelInstancePackerClusterID& ID)
	{
		return ID.GetHash();
	}

	FLevelInstancePackerCluster* GetData() const
	{
		return Data.Get();
	}

private:
	FLevelInstancePackerClusterID(const FLevelInstancePackerClusterID&) = delete;
	FLevelInstancePackerClusterID& operator=(const FLevelInstancePackerClusterID&) = delete;

	uint32 Hash;
	TUniquePtr<FLevelInstancePackerCluster> Data;
};

#endif