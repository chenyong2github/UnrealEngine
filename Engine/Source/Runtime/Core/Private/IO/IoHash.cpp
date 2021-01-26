// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoHash.h"

#include "Containers/UnrealString.h"

FString FIoHash::ToString() const
{
	FString Output;
	TArray<TCHAR>& CharArray = Output.GetCharArray();
	CharArray.AddUninitialized(sizeof(ByteArray) * 2 + 1);
	UE::String::BytesToHexLower(Hash, CharArray.GetData());
	CharArray.Last() = 0;
	return Output;
}
