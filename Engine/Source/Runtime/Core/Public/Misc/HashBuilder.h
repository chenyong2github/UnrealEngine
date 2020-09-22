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

	void Append(const void* Data, int64 Num)
	{
		Hash = FCrc::MemCrc32(Data, Num, Hash);
	}

	template <typename T>
	typename TEnableIf<TIsPODType<T>::Value, FHashBuilder&>::Type Append(const T& InData)
	{
		Append(&InData, sizeof(T));
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

/**
 * FArchive adapter for FHashBuilder
 */
class FHashBuilderArchive : public FArchive
{
public:
	FHashBuilderArchive()
	{
		SetIsLoading(false);
		SetIsSaving(true);
		SetIsPersistent(false);
	}

	virtual FString GetArchiveName() const { return TEXT("FHashBuilderArchive"); }

	void Serialize(void* Data, int64 Num) override { HashBuilder.Append(Data, Num); }

	using FArchive::operator<<;
	virtual FArchive& operator<<(class FName& Value) override { HashBuilder << Value; return *this; }
	virtual FArchive& operator<<(class UObject*& Value) override { check(0); return *this; }

	uint32 GetHash() const { return HashBuilder.GetHash(); }

protected:
	FHashBuilder HashBuilder;
};
