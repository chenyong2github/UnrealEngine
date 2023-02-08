// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

/**
 * A string that is present in memory only in editor and development builds.
 */
class ENGINE_API FStringDev
{
public:
	FStringDev()
	{}

	FStringDev(const FString& InString)
	{
		SetString(InString);
	}

	const FString& GetString() const;
	void SetString(const FString& InString);

	ENGINE_API friend FArchive& operator<<(FArchive& Ar, FStringDev& StringDev);

private:
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
	FString String;
#endif
};