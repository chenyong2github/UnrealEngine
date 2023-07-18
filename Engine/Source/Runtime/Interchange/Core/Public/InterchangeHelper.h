// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "CoreMinimal.h"

namespace UE::Interchange
{
	const TCHAR UnsupportedNameCharacters[] = { TEXT('.'), TEXT(','), TEXT('/'), TEXT('`'), TEXT('%') };
	const TCHAR UnsupportedJointNameCharacters[] = { TEXT('.'), TEXT(','), TEXT('/'), TEXT('`'), TEXT('%'), TEXT('+'), TEXT(' ') };

	/**
	* Replaces any unsupported characters with "_" character, and removes namespace indicator ":" character
	*/
	INTERCHANGECORE_API FString MakeName(const FString& InName, bool bIsJoint = false);
};