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
	FString MakeName(const FString& InName, bool bIsJoint = false)
	{
		FString SpecialChars = bIsJoint ? UnsupportedJointNameCharacters : UnsupportedNameCharacters;
		FString TmpName = InName;

		// Remove namespaces
		int32 LastNamespaceTokenIndex = INDEX_NONE;
		if (TmpName.FindLastChar(TEXT(':'), LastNamespaceTokenIndex))
		{
			const bool bAllowShrinking = true;
			//+1 to remove the ':' character we found
			TmpName.RightChopInline(LastNamespaceTokenIndex + 1, bAllowShrinking);
		}

		//Remove the special chars
		for (size_t CharIndex = 0; CharIndex < SpecialChars.Len(); CharIndex++)
		{
			TmpName.ReplaceCharInline(SpecialChars[CharIndex], TEXT('_'), ESearchCase::CaseSensitive);
		}

		return TmpName;
	}
};