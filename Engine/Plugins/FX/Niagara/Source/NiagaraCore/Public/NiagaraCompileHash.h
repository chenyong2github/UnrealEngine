// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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