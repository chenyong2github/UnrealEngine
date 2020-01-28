// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "NiagaraCompileHash.generated.h"

USTRUCT()
struct NIAGARACORE_API FNiagaraCompileHash
{
	GENERATED_BODY()

	FNiagaraCompileHash()
	{
	}

	explicit FNiagaraCompileHash(const TArray<uint8>& InDataHash)
	{
		checkf(InDataHash.Num() == HashSize, TEXT("Invalid hash data."));
		DataHash = InDataHash;
	}

	explicit FNiagaraCompileHash(const uint8* InDataHash, uint32 InCount) : DataHash(InDataHash, InCount)
	{
		checkf(InCount == HashSize, TEXT("Invalid hash data."));
		/*.AddUninitialized(InCount);
		for (uint32 i = 0; i < InCount; i++)
		{
			DataHash[i] = InDataHash[i];
		}*/
	}

	bool operator==(const FNiagaraCompileHash& Other) const;

	bool operator!=(const FNiagaraCompileHash& Other) const;

	bool IsValid() const;

	uint32 GetTypeHash() const;

	const uint8* GetData() const;

	FString ToString() const;

	NIAGARACORE_API friend FArchive& operator<<(FArchive& Ar, FNiagaraCompileHash& Id);

	static const uint32 HashSize;

private:
	UPROPERTY()
	TArray<uint8> DataHash;
};