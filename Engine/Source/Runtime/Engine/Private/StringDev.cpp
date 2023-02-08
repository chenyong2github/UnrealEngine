// Copyright Epic Games, Inc. All Rights Reserved.

#include "StringDev.h"

const FString& FStringDev::GetString() const
{
#if !WITH_EDITOR && !UE_BUILD_DEVELOPMENT
	static FString String;
#endif
	return String;
}

void FStringDev::SetString(const FString& InString)
{
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
	String = InString;
#endif
}

FArchive& operator<<(FArchive& Ar, FStringDev& StringDev)
{
#if !WITH_EDITOR && !UE_BUILD_DEVELOPMENT
	FString String;
	Ar << String;
#else
	Ar << StringDev.String;
#endif
	return Ar;
}