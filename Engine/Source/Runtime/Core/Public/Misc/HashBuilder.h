// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Crc.h"
#include "Containers/UnrealString.h"

class FHashBuilder
{
public:
	explicit FHashBuilder(uint32 InHash = 0)
		: Hash(~InHash)
	{}

	template <typename T>
	typename TEnableIf<TIsPODType<T>::Value, FHashBuilder&>::Type Append(const T& InData)
	{
		Hash = FCrc::MemCrc32(&InData, sizeof(T), Hash);
		return *this;
	}

	FHashBuilder& Append(const FString& InString)
	{
		uint32 StringHash = GetTypeHash(InString);
		return Append(StringHash);
	}

	FHashBuilder& Append(const FName& InName)
	{
		uint32 NameHash = GetTypeHash(InName);
		return Append(NameHash);
	}

	template <typename T>
	FHashBuilder& Append(const TArray<T>& InArray)
	{
		for (auto& Value: InArray)
		{
			Append(Value);
		}
		return *this;
	}

	template <typename T>
	FHashBuilder& Append(const TSet<T>& InArray)
	{
		for (auto& Value: InArray)
		{
			Append(Value);
		}
		return *this;
	}

	template <typename T>
	FHashBuilder& operator<<(const T& InData)
	{
		return Append(InData);
	}

	uint32 GetHash() const
	{
		return ~Hash;
	}

private:
	uint32 Hash;
};