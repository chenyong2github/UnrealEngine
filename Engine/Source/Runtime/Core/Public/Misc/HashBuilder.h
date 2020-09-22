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

	void AppendRaw(const void* Data, int64 Num)
	{
		Hash = FCrc::MemCrc32(Data, Num, Hash);
	}

	template <typename T>
	typename TEnableIf<TIsPODType<T>::Value, FHashBuilder&>::Type AppendRaw(const T& InData)
	{
		AppendRaw(&InData, sizeof(T), Hash);
		return *this;
	}

	template <typename T>
	typename TEnableIf<!TModels<CGetTypeHashable, T>::Value, FHashBuilder&>::Type Append(const T& InData)
	{
		return AppendRaw(InData);
	}

	template <typename T>
	typename TEnableIf<TModels<CGetTypeHashable, T>::Value, FHashBuilder&>::Type Append(const T& InData)
	{
		Hash = HashCombine(Hash, GetTypeHash(InData));
		return *this;
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

	void Serialize(void* Data, int64 Num) override { HashBuilder.AppendRaw(Data, Num); }

	using FArchive::operator<<;
	virtual FArchive& operator<<(class FName& Value) override { HashBuilder << Value; return *this; }
	virtual FArchive& operator<<(class UObject*& Value) override { check(0); return *this; }

	uint32 GetHash() const { return HashBuilder.GetHash(); }

protected:
	FHashBuilder HashBuilder;
};