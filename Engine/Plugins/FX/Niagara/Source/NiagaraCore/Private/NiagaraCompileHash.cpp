// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCompileHash.h"

const uint32 FNiagaraCompileHash::HashSize = 20;

bool FNiagaraCompileHash::operator==(const FNiagaraCompileHash& Other) const
{
	return DataHash == Other.DataHash;
}

bool FNiagaraCompileHash::operator!=(const FNiagaraCompileHash& Other) const
{
	return DataHash != Other.DataHash;
}

bool FNiagaraCompileHash::IsValid() const
{
	return DataHash.Num() == HashSize;
}

uint32 FNiagaraCompileHash::GetTypeHash() const
{
	// Use the first 4 bytes of the data hash as the hash id for this object.
	return DataHash.Num() == HashSize 
		? *((uint32*)(DataHash.GetData())) 
		: 0;
}

const uint8* FNiagaraCompileHash::GetData() const
{
	return DataHash.GetData();
}

FString FNiagaraCompileHash::ToString() const
{
	if (DataHash.Num() == HashSize)
	{
		FString DataHashString;
		DataHashString.Reserve(40);
		for (int i = 0; i < HashSize; i++)
		{
			DataHashString.Append(FString::Printf(TEXT("%02x"), DataHash[i]));
		}
		return DataHashString;
	}
	else
	{
		return TEXT("Invalid");
	}
}

FArchive& operator<<(FArchive& Ar, FNiagaraCompileHash& Id)
{
	return Ar << Id.DataHash;
}