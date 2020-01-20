// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/**
 * NetDebugName, carries both string pointer and a debugname id assigned by the persistent debug storage. DebugNameId is used to be able to avoid hashing already seen NetDebugNames
 */
typedef uint16 FNetDebugNameId;

struct FNetDebugName
{
	const TCHAR* Name = nullptr;
	mutable FNetDebugNameId DebugNameId = 0;
};

inline const TCHAR* ToCStr(const FNetDebugName* DebugName)
{
	if (DebugName && DebugName->Name)
	{
		return DebugName->Name;
	}
	else
	{
		return TEXT("N/A");
	}
}

